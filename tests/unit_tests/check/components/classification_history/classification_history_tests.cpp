/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
#include "glib.h"
#include "gst/analytics/analytics.h"
#include "gva_utils.h"
#include <classification_history.h>
#include <dlstreamer/gst/videoanalytics/tensor.h>
#include <gmock/gmock.h>
#include <gst/gstmeta.h>
#include <gstgvaclassify.h>
#include <gtest/gtest.h>
#include <test_common.h>
#include <test_utils.h>

#include <gst/check/gstcheck.h>

#include <gst/video/gstvideometa.h>

using ::testing::_;
using ::testing::Mock;
using ::testing::Return;
struct GVADetection {
    gfloat x_min;
    gfloat y_min;
    gfloat x_max;
    gfloat y_max;
    gdouble confidence;
    gint label_id;
    gint object_id;
};

struct Model {
    std::string name;
    std::string precision;
    std::string path;
};
struct TestData {
    size_t width;
    size_t height;
    std::vector<GVADetection> boxes;
};

std::map<std::string, TestData> test_data = {{"female", {620, 897, {{0.7964, 0.3644, 0.6252, 0.1769, 0.99, 1, 1}}}},
                                             {"male", {700, 698, {{0.6276, 0.3350, 0.6210, 0.2144, 0.99, 1, 1}}}}};

struct ClassificationHistoryTest : public ::testing::Test {

    // TODO: rename to SetUpTestSuite when migrate to googletest version higher than 1.8
    // https://github.com/google/googletest/blob/master/googletest/docs/advanced.md#sharing-resources-between-tests-in-the-same-test-suite
    // static void SetUpTestCase() {
    // }
    GstGvaClassify *gva_classify;
    ClassificationHistory *classification_history;
    LRUCache<int, ClassificationHistory::ROIClassificationHistory> history_lru_cache{CLASSIFICATION_HISTORY_SIZE};

    Model model;
    bool skip_classification;
    unsigned reclassify_interval;
    GstBuffer *buffer = nullptr;
    GstVideoRegionOfInterestMeta *meta = nullptr;
    GstAnalyticsODMtd od_mtd = {0, nullptr};
    std::vector<std::string> object_classes;

    GstBuffer *SetUpBuffer(const TestData &test_data, int id) {
        GstBuffer *custom_buf;
        cv::Mat image(620, 897, 3);
        size_t image_size = image.cols * image.rows * image.channels();
        custom_buf = gst_buffer_new_and_alloc(image_size);
        gst_buffer_fill(custom_buf, 0, image.data, image_size);

        GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(custom_buf);

        if (!relation_meta) {
            throw std::runtime_error("ClassificationHistoryTest: Failed to add GstAnalyticsRelationMeta to buffer");
        }

        for (auto input_bbox : test_data.boxes) {
            auto roi = gst_buffer_add_video_region_of_interest_meta(
                custom_buf, NULL, input_bbox.x_min * test_data.width, input_bbox.y_min * test_data.height,
                (input_bbox.x_max - input_bbox.x_min) * test_data.width,
                (input_bbox.y_max - input_bbox.y_min) * test_data.height);

            GstAnalyticsODMtd od_mtd;
            if (!gst_analytics_relation_meta_add_oriented_od_mtd(
                    relation_meta, 0, input_bbox.x_min * test_data.width, input_bbox.y_min * test_data.height,
                    (input_bbox.x_max - input_bbox.x_min) * test_data.width,
                    (input_bbox.y_max - input_bbox.y_min) * test_data.height, 0.0, 0.0, &od_mtd)) {
                throw std::runtime_error("ClassificationHistoryTest: Failed to add object detection metadata");
            }

            roi->id = od_mtd.id;
        }
        GstVideoRegionOfInterestMeta *roi = nullptr;
        void *state = nullptr;
        GstAnalyticsODMtd od_mtd;
        while (
            gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_od_mtd_get_mtd_type(), &od_mtd)) {
            roi = gst_buffer_get_video_region_of_interest_meta_id(custom_buf, od_mtd.id);

            if (!roi) {
                throw std::runtime_error("ClassificationHistoryTest: Failed to get video region of interest meta for "
                                         "object detection metadata");
            }

            set_object_id(roi, id);
            set_od_id(od_mtd, id);
        }

        return custom_buf;
    }
    static GstStructure *MakeClassificationTensor(const char *label, gint label_id, gdouble confidence) {
        return gst_structure_new("gender", "type", G_TYPE_STRING, GVA::GST_ANALYTICS_CLS_2_TENSOR, "label",
                                 G_TYPE_STRING, label, "label_id", G_TYPE_INT, label_id, "confidence", G_TYPE_DOUBLE,
                                 confidence, "model_name", G_TYPE_STRING, "gender_model", NULL);
    }

    static GstAnalyticsODMtd GetFirstOD(GstBuffer *buf) {
        GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buf);
        GstAnalyticsODMtd od = {0, nullptr};
        gpointer state = nullptr;
        if (!relation_meta ||
            !gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_od_mtd_get_mtd_type(), &od))
            throw std::runtime_error("ClassificationHistoryTest: no object detection metadata");
        return od;
    }

    // Simulates how meta attacher attaches classification result to object detection metadata
    static void AttachClassification(GstAnalyticsODMtd od, GstStructure *s, const std::vector<GQuark> &labels) {
        std::vector<gfloat> levels(labels.size(), 0.0f);
        std::vector<GQuark> quarks(labels);
        GstAnalyticsClsMtd descriptor;
        ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(od.meta, quarks.size(), levels.data(), quarks.data(),
                                                            &descriptor));
        gst_analytics_mtd_set_semantic_tag(reinterpret_cast<GstAnalyticsMtd *>(&descriptor), "class_descriptor");

        GstAnalyticsMtd mtd;
        ASSERT_TRUE(GVA::Tensor(s).convert_to_meta(&mtd, od.meta));
        ASSERT_TRUE(gst_analytics_relation_meta_set_relation(od.meta, GST_ANALYTICS_REL_TYPE_CONTAIN, od.id, mtd.id));
        ASSERT_TRUE(
            gst_analytics_relation_meta_set_relation(od.meta, GST_ANALYTICS_REL_TYPE_IS_PART_OF, mtd.id, od.id));
        ASSERT_TRUE(gst_analytics_relation_meta_set_relation(od.meta, GST_ANALYTICS_REL_TYPE_RELATE_TO, mtd.id,
                                                             descriptor.id));
    }

    static std::vector<GstStructureUniquePtr> GetClassifications(GstAnalyticsODMtd od) {
        std::vector<GstStructureUniquePtr> result;
        gpointer state = nullptr;
        GstAnalyticsMtd mtd;
        while (gst_analytics_relation_meta_get_direct_related(od.meta, od.id, GST_ANALYTICS_REL_TYPE_CONTAIN,
                                                              gst_analytics_cls_mtd_get_mtd_type(), &state, &mtd))
            result.emplace_back(GVA::Tensor::convert_to_tensor(mtd), gst_structure_free);
        return result;
    }

    void SetUpModel(std::string _name, std::string _precision = "FP32") {
        model.name = _name;
        model.precision = _precision;

        char temp_c_str[MAX_STR_PATH_SIZE] = {};
        ExitStatus exit_status =
            get_model_path(temp_c_str, MAX_STR_PATH_SIZE, model.name.c_str(), model.precision.c_str());
        EXPECT_EQ(exit_status, EXIT_SUCCESS);
        model.path = std::string(temp_c_str);
        gva_classify->base_inference.model = g_strdup(model.path.c_str());

        gva_classify->base_inference.model_proc = g_strdup(model.name.c_str());
    }

    void SetUp() {
        gva_classify = GST_GVA_CLASSIFY(g_object_new(gst_gva_classify_get_type(), NULL));
        GvaBaseInference *gva_base_inference = GVA_BASE_INFERENCE(gva_classify);
        gva_base_inference->inference_region = ROI_LIST;
        gva_base_inference->object_class = nullptr;

        // InferenceImpl object has object_classes vector as first member,
        // so we can avoid creating and destroying heavy InferenceImpl instances
        // with lots of dependent parameters unnecessary in these tests
        // using this hack (cast vector pointer to InferenceImpl pointer)
        gva_base_inference->inference = reinterpret_cast<InferenceImpl *>(&object_classes);

        classification_history = gva_classify->classification_history;
        buffer = gst_buffer_new_and_alloc(100);

        const gchar *label = "label";
        GQuark label_quark = g_quark_from_string(label);

        meta = gst_buffer_add_video_region_of_interest_meta(buffer, label, 0, 0, 0, 0);

        GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(buffer);

        if (!relation_meta) {
            throw std::runtime_error("ClassificationHistoryTest: Failed to add GstAnalyticsRelationMeta to buffer");
        }

        if (!gst_analytics_relation_meta_add_oriented_od_mtd(relation_meta, label_quark, 0, 0, 0, 0, 0.0, 0.0,
                                                             &od_mtd)) {
            throw std::runtime_error("ClassificationHistoryTest: Failed to add object detection metadata");
        }

        meta->id = od_mtd.id;

        SetUpModel("dima806_fairface_gender_image_detection");
    }

    void TearDown() {
        gva_classify->base_inference.inference = nullptr;
        g_object_unref(gva_classify);
        if (buffer)
            gst_buffer_unref(buffer);
    }
};

TEST_F(ClassificationHistoryTest, IsRoiClassificationNeeded_zero_roi_id_zero_frame) {
    set_object_id(meta, 0);

    EXPECT_TRUE(classification_history->IsROIClassificationNeeded(meta, buffer, 0));
}
TEST_F(ClassificationHistoryTest, IsRoiClassificationNeeded_zero_roi_id) {
    set_object_id(meta, 1);
    EXPECT_TRUE(classification_history->IsROIClassificationNeeded(meta, buffer, 3));
}

TEST_F(ClassificationHistoryTest, IsRoiClassificationNeeded_not_in_history_id_zero_frame) {
    set_object_id(meta, 12);
    ASSERT_TRUE(classification_history->IsROIClassificationNeeded(meta, buffer, 0));
}
TEST_F(ClassificationHistoryTest, IsRoiClassificationNeeded_not_in_history_id) {
    set_object_id(meta, 1);
    ASSERT_TRUE(classification_history->IsROIClassificationNeeded(meta, buffer, 2));
}

TEST_F(ClassificationHistoryTest, UpdateROIParams_test) {
    set_od_id(od_mtd, 1);
    GstStructureUniquePtr tensor(MakeClassificationTensor("male", 0, 0.8), gst_structure_free);

    classification_history->UpdateROIParams(buffer, *meta, {tensor.get()});

    ASSERT_EQ(classification_history->GetHistory().count(1), 1);
    const auto &roi_history = classification_history->GetHistory().get(1);
    EXPECT_EQ(roi_history.frame_of_last_result, 0u);
    ASSERT_EQ(roi_history.results.size(), 1u);
    EXPECT_STREQ(gst_structure_get_string(roi_history.results[0].tensor.get(), "label"), "male");
    EXPECT_TRUE(roi_history.results[0].class_descriptor.empty());
}

TEST_F(ClassificationHistoryTest, UpdateROIParams_untracked_object_test) {
    GstStructureUniquePtr tensor(MakeClassificationTensor("male", 0, 0.8), gst_structure_free);

    classification_history->UpdateROIParams(buffer, *meta, {tensor.get()});

    EXPECT_EQ(classification_history->GetHistory().size(), 0u);
}

TEST_F(ClassificationHistoryTest, ClassificationHistory_test) {
    gva_classify->reclassify_interval = 3;
    set_object_id(meta, 1);
    set_od_id(od_mtd, 1);
    gint id;
    get_od_id(od_mtd, &id);
    gint roi_id;
    get_object_id(meta, &roi_id);
    ASSERT_EQ(id, roi_id);
    GstStructureUniquePtr some_params(MakeClassificationTensor("male", 0, 0.8), gst_structure_free);

    ASSERT_TRUE(classification_history->IsROIClassificationNeeded(meta, buffer, 0));

    classification_history->UpdateROIParams(buffer, *meta, {some_params.get()});
    ASSERT_FALSE(classification_history->IsROIClassificationNeeded(meta, buffer, 1));
}

TEST_F(ClassificationHistoryTest, ClassificationHistory_advance_test) {
    gva_classify->reclassify_interval = 4;
    set_object_id(meta, 1);
    set_od_id(od_mtd, 1);
    gint id;
    get_od_id(od_mtd, &id);
    gint roi_id;
    get_object_id(meta, &roi_id);
    ASSERT_EQ(id, roi_id);
    GstStructureUniquePtr some_params(MakeClassificationTensor("male", 0, 0.8), gst_structure_free);

    size_t start_num_frame = 3;
    ASSERT_TRUE(classification_history->IsROIClassificationNeeded(meta, buffer, start_num_frame));
    classification_history->UpdateROIParams(buffer, *meta, {some_params.get()});
    size_t i = 1;
    // we do not proceed reclassification until gva_classify->reclassify_interval-th frame.
    // So there must be <, not <=
    for (; i < gva_classify->reclassify_interval; ++i) {
        ASSERT_FALSE(classification_history->IsROIClassificationNeeded(meta, buffer, i + start_num_frame));
    }
    ASSERT_TRUE(classification_history->IsROIClassificationNeeded(meta, buffer, i + start_num_frame));
}

TEST_F(ClassificationHistoryTest, FillROIParams_test) {
    gva_classify->reclassify_interval = 4;
    const int object_id = 13;
    const std::vector<GQuark> labels = {g_quark_from_string("male"), g_quark_from_string("female")};
    GstStructureUniquePtr tensor(MakeClassificationTensor("female", 1, 0.9), gst_structure_free);

    // frame 0: object is classified, results are attached by inference (meta attacher)
    GstBuffer *frame0 = SetUpBuffer(test_data["female"], object_id);
    GstAnalyticsODMtd od0 = GetFirstOD(frame0);
    GstVideoRegionOfInterestMeta *roi0 = gst_buffer_get_video_region_of_interest_meta_id(frame0, od0.id);
    ASSERT_NE(roi0, nullptr);
    ASSERT_TRUE(classification_history->IsROIClassificationNeeded(roi0, frame0, 0));
    AttachClassification(od0, tensor.get(), labels);
    classification_history->UpdateROIParams(frame0, *roi0, {tensor.get()});

    ASSERT_EQ(classification_history->GetHistory().get(object_id).results.size(), 1u);
    EXPECT_EQ(classification_history->GetHistory().get(object_id).results[0].class_descriptor, labels);

    // results must not be duplicated on the frame the object has been classified on
    classification_history->FillROIParams(frame0);
    EXPECT_EQ(GetClassifications(od0).size(), 1u);

    // frame 1: object is not classified, results must be restored from history as GstAnalytics metadata
    GstBuffer *frame1 = SetUpBuffer(test_data["female"], object_id);
    GstAnalyticsODMtd od1 = GetFirstOD(frame1);
    GstVideoRegionOfInterestMeta *roi1 = gst_buffer_get_video_region_of_interest_meta_id(frame1, od1.id);
    ASSERT_NE(roi1, nullptr);
    ASSERT_FALSE(classification_history->IsROIClassificationNeeded(roi1, frame1, 1));
    ASSERT_TRUE(GetClassifications(od1).empty());

    classification_history->FillROIParams(frame1);

    auto classifications = GetClassifications(od1);
    ASSERT_EQ(classifications.size(), 1u);
    GVA::Tensor restored(classifications[0].get());
    EXPECT_EQ(restored.label(), "female");
    EXPECT_EQ(restored.get_int("label_id", -1), 1);
    EXPECT_NEAR(restored.confidence(), 0.9, 1e-6);
    EXPECT_EQ(restored.get_string("semantic_tag"), "gender_model");

    // legacy GstVideoRegionOfInterestMeta param is restored as well (still used by e.g. gvametaconvert)
    const GstStructure *legacy = gst_video_region_of_interest_meta_get_param(roi1, gst_structure_get_name(tensor.get()));
    ASSERT_NE(legacy, nullptr);
    EXPECT_STREQ(gst_structure_get_string(legacy, "label"), "female");
    gint frames_ago = -1;
    EXPECT_TRUE(gst_structure_get_int(legacy, "frames_ago", &frames_ago));
    EXPECT_EQ(frames_ago, 1);

    gst_buffer_unref(frame0);
    gst_buffer_unref(frame1);
}

TEST_F(ClassificationHistoryTest, FillROIParams_pending_reclassification_test) {
    // Input side of the element runs ahead of the output side (asynchronous inference). Objects scheduled
    // for reclassification on later frames must not affect results restored for earlier frames.
    gva_classify->reclassify_interval = 2;
    const int object_id = 7;
    GstStructureUniquePtr result_a(MakeClassificationTensor("male", 0, 0.6), gst_structure_free);
    GstStructureUniquePtr result_b(MakeClassificationTensor("female", 1, 0.7), gst_structure_free);

    std::vector<GstBuffer *> frames;
    for (int i = 0; i < 4; ++i)
        frames.push_back(SetUpBuffer(test_data["female"], object_id));
    auto roi_of = [&](int i) {
        return gst_buffer_get_video_region_of_interest_meta_id(frames[i], GetFirstOD(frames[i]).id);
    };

    // input side: frames 0..3
    ASSERT_TRUE(classification_history->IsROIClassificationNeeded(roi_of(0), frames[0], 0));
    ASSERT_FALSE(classification_history->IsROIClassificationNeeded(roi_of(1), frames[1], 1));
    ASSERT_TRUE(classification_history->IsROIClassificationNeeded(roi_of(2), frames[2], 2));
    ASSERT_FALSE(classification_history->IsROIClassificationNeeded(roi_of(3), frames[3], 3));

    // output side: frame 0 classified with result A
    classification_history->UpdateROIParams(frames[0], *roi_of(0), {result_a.get()});
    classification_history->FillROIParams(frames[0]);
    EXPECT_TRUE(GetClassifications(GetFirstOD(frames[0])).empty());

    // frame 1 gets result A although reclassification on frame 2 has already been scheduled
    classification_history->FillROIParams(frames[1]);
    auto frame1_results = GetClassifications(GetFirstOD(frames[1]));
    ASSERT_EQ(frame1_results.size(), 1u);
    EXPECT_EQ(GVA::Tensor(frame1_results[0].get()).label(), "male");

    // frame 2 classified with result B
    classification_history->UpdateROIParams(frames[2], *roi_of(2), {result_b.get()});
    classification_history->FillROIParams(frames[2]);
    EXPECT_TRUE(GetClassifications(GetFirstOD(frames[2])).empty());

    // frame 3 gets result B
    classification_history->FillROIParams(frames[3]);
    auto frame3_results = GetClassifications(GetFirstOD(frames[3]));
    ASSERT_EQ(frame3_results.size(), 1u);
    EXPECT_EQ(GVA::Tensor(frame3_results[0].get()).label(), "female");

    for (GstBuffer *frame : frames)
        gst_buffer_unref(frame);
}

TEST_F(ClassificationHistoryTest, ReclassifyIntervalChange_clears_outdated_history_test) {
    classification_history->GetHistory().put(1);
    classification_history->GetHistory().put(2);

    // history is up to date when switching between intervals greater than 1
    g_object_set(gva_classify, "reclassify-interval", 5, NULL);
    g_object_set(gva_classify, "reclassify-interval", 3, NULL);
    ASSERT_EQ(classification_history->GetHistory().size(), 0u); // cleared on initial 1 -> 5 switch

    classification_history->GetHistory().put(1);
    g_object_set(gva_classify, "reclassify-interval", 4, NULL);
    EXPECT_EQ(classification_history->GetHistory().size(), 1u);

    // history is not maintained with interval 1, so it is outdated after switching back
    g_object_set(gva_classify, "reclassify-interval", 1, NULL);
    EXPECT_EQ(classification_history->GetHistory().size(), 1u);
    g_object_set(gva_classify, "reclassify-interval", 2, NULL);
    EXPECT_EQ(classification_history->GetHistory().size(), 0u);
}

TEST_F(ClassificationHistoryTest, ClassificationHistory_LRUCache_api_test) {
    // test LRU Cache API sanity interaction with ROIClassificationHistory defined for classification history operations

    gint id1 = 1;
    std::string struct1 = "struct1";
    auto some_params1 = GstStructureSharedPtr(gst_structure_new_empty(struct1.c_str()), gst_structure_free);

    gint id2 = 2;
    std::string struct2 = "struct2";
    auto some_params2 = GstStructureSharedPtr(gst_structure_new_empty(struct2.c_str()), gst_structure_free);
    ClassificationHistory::ROIClassificationHistory id2_history(2, 2, {{some_params2, {}}});

    std::string struct2_new = "struct2_new";
    auto some_params2_new = GstStructureSharedPtr(gst_structure_new_empty(struct2_new.c_str()), gst_structure_free);
    ClassificationHistory::ROIClassificationHistory id2_new_history(3, 3, {{some_params2_new, {}}});

    ASSERT_TRUE(history_lru_cache.count(id1) == 0);
    ASSERT_THROW(history_lru_cache.get(id1), std::runtime_error);

    // put new object and fill later (put key with no value and set value later)
    history_lru_cache.put(id1);
    ASSERT_TRUE(history_lru_cache.count(id1) == 1);
    history_lru_cache.get(id1).results.push_back({some_params1, {}});
    history_lru_cache.get(id1).frame_of_last_update = 1;

    ClassificationHistory::ROIClassificationHistory id1_history_test = history_lru_cache.get(id1);
    ASSERT_EQ(id1_history_test.frame_of_last_update, 1);
    ASSERT_EQ(id1_history_test.results.size(), 1u);
    ASSERT_EQ(id1_history_test.results[0].tensor, some_params1);

    // put already filled object (put key and value)
    history_lru_cache.put(id2, id2_history);
    ASSERT_TRUE(history_lru_cache.count(id2) == 1);

    ClassificationHistory::ROIClassificationHistory id2_history_test = history_lru_cache.get(id2);
    ASSERT_EQ(id2_history_test.frame_of_last_update, 2);
    ASSERT_EQ(id2_history_test.results.size(), 1u);
    ASSERT_EQ(id2_history_test.results[0].tensor, some_params2);

    // update existing object (update existing key with new value)
    history_lru_cache.put(id2, id2_new_history);
    ASSERT_TRUE(history_lru_cache.count(id2) == 1);

    ClassificationHistory::ROIClassificationHistory id2_new_history_test = history_lru_cache.get(id2);
    ASSERT_EQ(id2_new_history_test.frame_of_last_update, 3);
    ASSERT_EQ(id2_new_history_test.frame_of_last_result, 3u);
    ASSERT_EQ(id2_new_history_test.results.size(), 1u);
    ASSERT_EQ(id2_new_history_test.results[0].tensor, some_params2_new);
}

TEST_F(ClassificationHistoryTest, ClassificationHistory_LRUCache_size_test) {
    // test LRU cache functionality - exceeding cache capacity and checking that least used items are removed

    ASSERT_EQ(history_lru_cache.size(), 0);

    // fill LRU cache at maximum capacity
    for (unsigned i = 0; i < CLASSIFICATION_HISTORY_SIZE; i++)
        history_lru_cache.put(i);
    for (unsigned i = 0; i < CLASSIFICATION_HISTORY_SIZE; i++)
        ASSERT_NO_THROW(history_lru_cache.get(i));
    ASSERT_THROW(history_lru_cache.get(CLASSIFICATION_HISTORY_SIZE), std::runtime_error);
    ASSERT_EQ(history_lru_cache.size(), CLASSIFICATION_HISTORY_SIZE);

    // adding one more object over size, so expect object with 0 id to be removed from LRU cache
    history_lru_cache.put(CLASSIFICATION_HISTORY_SIZE);
    for (unsigned i = 1; i < CLASSIFICATION_HISTORY_SIZE + 1; i++)
        ASSERT_NO_THROW(history_lru_cache.get(i));
    ASSERT_THROW(history_lru_cache.get(0), std::runtime_error);
    ASSERT_EQ(history_lru_cache.size(), CLASSIFICATION_HISTORY_SIZE);

    // any operation on object with id CLASSIFICATION_HISTORY_SIZE does not change LRU cache contents
    std::string test_struct = "struct";
    auto some_params = GstStructureSharedPtr(gst_structure_new_empty(test_struct.c_str()), gst_structure_free);
    gst_structure_set_name(some_params.get(), test_struct.c_str());
    ClassificationHistory::ROIClassificationHistory test_history(1, 1, {{some_params, {}}});

    history_lru_cache.get(CLASSIFICATION_HISTORY_SIZE);
    history_lru_cache.put(CLASSIFICATION_HISTORY_SIZE, {});
    history_lru_cache.put(CLASSIFICATION_HISTORY_SIZE, test_history);
    history_lru_cache.put(CLASSIFICATION_HISTORY_SIZE);
    for (unsigned i = 1; i < CLASSIFICATION_HISTORY_SIZE + 1; i++)
        ASSERT_NO_THROW(history_lru_cache.get(i));
    ASSERT_THROW(history_lru_cache.get(0), std::runtime_error);
    ASSERT_EQ(history_lru_cache.size(), CLASSIFICATION_HISTORY_SIZE);

    // access objects with ids 2 and 1, so that they become the most recent objects and will be removed from LRU cache
    // after objects with ids from 3 to CLASSIFICATION_HISTORY_SIZE are removed
    history_lru_cache.put(2);
    history_lru_cache.get(1);
    for (unsigned i = CLASSIFICATION_HISTORY_SIZE + 1; i < 2 * CLASSIFICATION_HISTORY_SIZE - 1; i++)
        history_lru_cache.put(i);
    for (unsigned i = 3; i < CLASSIFICATION_HISTORY_SIZE + 1; i++)
        ASSERT_THROW(history_lru_cache.get(i), std::runtime_error);
    for (unsigned i = CLASSIFICATION_HISTORY_SIZE + 1; i < 2 * CLASSIFICATION_HISTORY_SIZE - 1; i++)
        ASSERT_NO_THROW(history_lru_cache.get(i));
    history_lru_cache.put(2 * CLASSIFICATION_HISTORY_SIZE - 1);
    ASSERT_THROW(history_lru_cache.get(2), std::runtime_error); // obj with id 1 is more recent then object with id 2
    ASSERT_EQ(history_lru_cache.size(), CLASSIFICATION_HISTORY_SIZE);

    // put object with id 2, and this will finally remove obj with id 1, which lived for the longest time due to recent
    // accesses
    history_lru_cache.put(2);
    ASSERT_NO_THROW(history_lru_cache.get(2));
    for (unsigned i = CLASSIFICATION_HISTORY_SIZE + 1; i < 2 * CLASSIFICATION_HISTORY_SIZE - 1; i++)
        ASSERT_NO_THROW(history_lru_cache.get(i));
    ASSERT_THROW(history_lru_cache.get(1), std::runtime_error);
    ASSERT_EQ(history_lru_cache.size(), CLASSIFICATION_HISTORY_SIZE);
}
