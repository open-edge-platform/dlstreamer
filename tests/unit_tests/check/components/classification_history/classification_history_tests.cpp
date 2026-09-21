/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
#include "glib.h"
#include "gst/analytics/analytics.h"
#include "gva_utils.h"
#include <classification_history.h>
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
    std::string proc_path;
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
    void SetUpModel(std::string _name, std::string _precision = "FP32") {
        model.name = _name;
        model.precision = _precision;

        char temp_c_str[MAX_STR_PATH_SIZE] = {};
        ExitStatus exit_status =
            get_model_path(temp_c_str, MAX_STR_PATH_SIZE, model.name.c_str(), model.precision.c_str());
        EXPECT_EQ(exit_status, EXIT_SUCCESS);
        model.path = std::string(temp_c_str);
        gva_classify->base_inference.model = g_strdup(model.path.c_str());

        exit_status = get_model_proc_path(temp_c_str, MAX_STR_PATH_SIZE, model.name.c_str());
        EXPECT_EQ(exit_status, EXIT_SUCCESS);
        model.proc_path = std::string(temp_c_str);
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

TEST_F(ClassificationHistoryTest, UpdateRoiParamsHistory_test) {
    set_object_id(meta, 1);
    std::string structure_name = "some_params";
    GstStructure *some_params = gst_structure_new_empty(structure_name.c_str());
    gst_structure_set_name(some_params, structure_name.c_str());
    gint id;
    get_object_id(meta, &id);
    classification_history->GetHistory().put(id);
    classification_history->UpdateROIParams(id, some_params);
    ASSERT_EQ(classification_history->GetHistory().count(id), 1);
    ASSERT_EQ(classification_history->GetHistory().get(id).layers_to_roi_params.count(structure_name), 1);
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
    std::string structure_name = "some_params";
    GstStructure *some_params = gst_structure_new_empty(structure_name.c_str());
    gst_structure_set_name(some_params, structure_name.c_str());

    classification_history->IsROIClassificationNeeded(meta, buffer, 0);

    classification_history->UpdateROIParams(id, some_params);
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
    std::string structure_name = "some_params";
    GstStructure *some_params = gst_structure_new_empty(structure_name.c_str());
    gst_structure_set_name(some_params, structure_name.c_str());

    size_t start_num_frame = 3;
    ASSERT_TRUE(classification_history->IsROIClassificationNeeded(meta, buffer, start_num_frame));
    size_t i = 1;
    // we do not proceed reclassification until gva_classify->reclassify_interval-th frame.
    // So there must be <, not <=
    for (; i < gva_classify->reclassify_interval; ++i) {
        classification_history->UpdateROIParams(id, some_params);
        ASSERT_FALSE(classification_history->IsROIClassificationNeeded(meta, buffer, i + start_num_frame));
    }
    classification_history->UpdateROIParams(id, some_params);
    ASSERT_TRUE(classification_history->IsROIClassificationNeeded(meta, buffer, i + start_num_frame));
}

TEST_F(ClassificationHistoryTest, FillROIParams_test) {
    GstBuffer *image_buf = SetUpBuffer(test_data["female"], 13);
    gva_classify->base_inference.info = gst_video_info_new();
    gst_video_info_set_format(gva_classify->base_inference.info, GST_VIDEO_FORMAT_BGRA, test_data["female"].width,
                              test_data["female"].height);
    gva_classify->reclassify_interval = 4;

    void *state = nullptr;

    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(image_buf);
    ASSERT_NE(relation_meta, nullptr);

    GstAnalyticsODMtd od_meta;
    gboolean ret =
        gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_od_mtd_get_mtd_type(), &od_meta);
    ASSERT_TRUE(ret);

    GstVideoRegionOfInterestMeta *roi = gst_buffer_get_video_region_of_interest_meta_id(image_buf, od_meta.id);
    ASSERT_NE(roi, nullptr);

    set_object_id(roi, 13);
    set_od_id(od_meta, 13);
    int id;
    get_od_id(od_meta, &id);
    int roi_id;
    get_object_id(roi, &roi_id);
    ASSERT_EQ(id, roi_id);
    std::string structure_name = "some_params";
    GstStructure *input_params = gst_structure_new_empty(structure_name.c_str());
    gst_structure_set_name(input_params, structure_name.c_str());

    ASSERT_TRUE(classification_history->IsROIClassificationNeeded(roi, image_buf, 0));
    classification_history->UpdateROIParams(id, input_params);
    ASSERT_FALSE(classification_history->IsROIClassificationNeeded(roi, image_buf, 1));

    ASSERT_NO_THROW(classification_history->FillROIParams(image_buf));

    state = nullptr;
    roi = nullptr;
    od_meta = {0, nullptr};
    GstStructure *output_params = nullptr;

    ret = gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_od_mtd_get_mtd_type(), &od_meta);
    ASSERT_TRUE(ret);

    if ((roi = gst_buffer_get_video_region_of_interest_meta_id(image_buf, od_meta.id))) {
        output_params = gst_video_region_of_interest_meta_get_param(roi, structure_name.c_str());
    }

    ASSERT_FALSE(output_params == nullptr);
}

TEST_F(ClassificationHistoryTest, ClassificationHistory_LRUCache_api_test) {
    // test LRU Cache API sanity interaction with ROIClassificationHistory defined for classification history operations

    gint id1 = 1;
    std::string struct1 = "struct1";
    auto some_params1 = GstStructureSharedPtr(gst_structure_new_empty(struct1.c_str()), gst_structure_free);
    gst_structure_set_name(some_params1.get(), struct1.c_str());

    gint id2 = 2;
    std::string struct2 = "struct2";
    auto some_params2 = GstStructureSharedPtr(gst_structure_new_empty(struct2.c_str()), gst_structure_free);
    gst_structure_set_name(some_params2.get(), struct2.c_str());
    ClassificationHistory::ROIClassificationHistory id2_history(2, {{std::string("layer2"), some_params2}});

    std::string struct2_new = "struct2_new";
    auto some_params2_new = GstStructureSharedPtr(gst_structure_new_empty(struct2_new.c_str()), gst_structure_free);
    gst_structure_set_name(some_params2_new.get(), struct2_new.c_str());
    ClassificationHistory::ROIClassificationHistory id2_new_history(3, {{std::string("layer3"), some_params2_new}});

    ASSERT_TRUE(history_lru_cache.count(id1) == 0);
    ASSERT_THROW(history_lru_cache.get(id1), std::runtime_error);

    // put new object and fill later (put key with no value and set value later)
    history_lru_cache.put(id1);
    ASSERT_TRUE(history_lru_cache.count(id1) == 1);
    history_lru_cache.get(id1).layers_to_roi_params["layer1"] = some_params1;
    history_lru_cache.get(id1).frame_of_last_update = 1;

    ClassificationHistory::ROIClassificationHistory id1_history_test = history_lru_cache.get(id1);
    ASSERT_EQ(id1_history_test.frame_of_last_update, 1);
    ASSERT_EQ(id1_history_test.layers_to_roi_params["layer1"], some_params1);

    // put already filled object (put key and value)
    history_lru_cache.put(id2, id2_history);
    ASSERT_TRUE(history_lru_cache.count(id2) == 1);

    ClassificationHistory::ROIClassificationHistory id2_history_test = history_lru_cache.get(id2);
    ASSERT_EQ(id2_history_test.frame_of_last_update, 2);
    ASSERT_EQ(id2_history_test.layers_to_roi_params["layer2"], some_params2);

    // update existing object (update existing key with new value)
    history_lru_cache.put(id2, id2_new_history);
    ASSERT_TRUE(history_lru_cache.count(id2) == 1);

    ClassificationHistory::ROIClassificationHistory id2_new_history_test = history_lru_cache.get(id2);
    ASSERT_EQ(id2_new_history_test.frame_of_last_update, 3);
    ASSERT_EQ(id2_new_history_test.layers_to_roi_params.count("layer2"), 0);
    ASSERT_EQ(id2_new_history_test.layers_to_roi_params["layer3"], some_params2_new);
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
    ClassificationHistory::ROIClassificationHistory test_history(1, {{std::string("test_layer"), some_params}});

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
