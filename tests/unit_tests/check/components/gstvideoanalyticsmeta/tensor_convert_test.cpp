/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file tensor_convert_test.cpp
 * @brief Unit tests for GVA::Tensor::convert_to_meta() and
 *        GVA::Tensor::convert_to_tensor() roundtrip conversions between
 *        GstStructure tensors and GstAnalytics metadata.
 */

#include "dlstreamer/gst/videoanalytics/tensor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <utility>

#include <glib.h>
#include <gmock/gmock.h>
#include <gst/analytics/analytics.h>
#include <gst/analytics/gstanalyticssegmentationmtd.h>
#include <gst/gstbuffer.h>
#include <gst/video/gstvideometa.h>
#include <gtest/gtest.h>

#include "dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h"
#include <gst/analytics/gstanalyticskeypointmtd.h>

// ── Test constants ──────────────────────────────────────────────────────────

static constexpr gsize KEYPOINT_COUNT = 17;
static constexpr gsize KEYPOINT_DIM = 2;

// Bounding box (pixel coordinates)
static constexpr gint OD_X = 100;
static constexpr gint OD_Y = 50;
static constexpr gint OD_W = 200;
static constexpr gint OD_H = 400;
static constexpr gfloat OD_CONF = 0.9f;

// Normalized float positions (17 keypoints x 2 coords) – same as Python tests
static const float KEYPOINT_POSITIONS_NORM[] = {
    0.50f, 0.30f, // nose
    0.52f, 0.28f, // eye_l
    0.48f, 0.28f, // eye_r
    0.56f, 0.27f, // ear_l
    0.44f, 0.27f, // ear_r
    0.60f, 0.40f, // shoulder_l
    0.40f, 0.40f, // shoulder_r
    0.65f, 0.55f, // elbow_l
    0.35f, 0.55f, // elbow_r
    0.63f, 0.70f, // wrist_l
    0.37f, 0.70f, // wrist_r
    0.58f, 0.70f, // hip_l
    0.42f, 0.70f, // hip_r
    0.60f, 0.85f, // knee_l
    0.40f, 0.85f, // knee_r
    0.61f, 0.95f, // ankle_l
    0.39f, 0.95f, // ankle_r
};

static const float KEYPOINT_CONFIDENCES[] = {
    0.97998046875f, 0.9677734375f,  0.8984375f,   0.888671875f,   0.416259765625f, 0.9921875f,     0.98388671875f,
    0.9677734375f,  0.91845703125f, 0.947265625f, 0.88818359375f, 0.97412109375f,  0.96240234375f, 0.0f,
    0.71826171875f, 0.0f,           0.0f,
};

// Classification test data — from real pipeline output
static const char *CLS_LABEL = "neutral";
static constexpr double CLS_CONFIDENCE = 0.53389871120452881;
static constexpr int CLS_LABEL_ID = 5;
static constexpr int CLS_TENSOR_ID = 0;
// Classification softmax output data (1x10 tensor)
static const float CLS_DATA[] = {
    0.01f, 0.02f, 0.05f, 0.03f, 0.10f, 0.534f, 0.08f, 0.06f, 0.04f, 0.076f,
};

// ── Helper: get keypoint descriptors ─────────────────────────────────────────

static const GstAnalyticsKeypointDescriptor *coco17_descriptor() {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    g_assert(desc != nullptr);
    return desc;
}

static const GstAnalyticsKeypointDescriptor *openpose18_descriptor() {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_OPENPOSE_18);
    g_assert(desc != nullptr);
    return desc;
}

// ── Helper: build a keypoints GVA::Tensor (matching real pipeline output) ───

static GstStructure *build_keypoint_structure() {
    const auto *desc = coco17_descriptor();
    GstStructure *s = gst_structure_new_empty(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    GVA::Tensor tensor(s);

    tensor.set_double("iou_threshold", 0.7);
    tensor.set_string("converter", "yolo_v11_pose");
    tensor.set_double("confidence_threshold", 0.5);
    tensor.set_string("layer_name", "output");
    tensor.set_string("model_name", "Model0");
    tensor.set_type(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    tensor.set_format(desc->semantic_tag);
    tensor.set_dims({static_cast<guint>(KEYPOINT_COUNT), static_cast<guint>(KEYPOINT_DIM)});
    tensor.set_data(KEYPOINT_POSITIONS_NORM, KEYPOINT_COUNT * KEYPOINT_DIM * sizeof(float));
    tensor.set_precision(GVA::Tensor::Precision::FP32);
    tensor.set_vector<float>("confidence",
                             std::vector<float>(KEYPOINT_CONFIDENCES, KEYPOINT_CONFIDENCES + KEYPOINT_COUNT));
    tensor.set_vector<std::string>("point_names",
                                   std::vector<std::string>(desc->point_names, desc->point_names + desc->point_count));
    tensor.set_vector<uint32_t>(
        "point_connections", std::vector<uint32_t>(desc->skeleton_connections,
                                                   desc->skeleton_connections + desc->skeleton_connection_count * 2));

    return s;
}

// ── Helper: build a classification GVA::Tensor (matching real pipeline output)

static GstStructure *build_classification_structure() {
    GstStructure *s = gst_structure_new_empty("classification_layer_name:output");
    GVA::Tensor tensor(s);

    tensor.set_string("converter", "label");
    tensor.set_string("method", "softmax");
    tensor.set_string("layer_name", "output");
    tensor.set_string("model_name", "torch_jit");
    tensor.set_data(CLS_DATA, sizeof(CLS_DATA));
    tensor.set_precision(GVA::Tensor::Precision::FP32);
    tensor.set_layout(GVA::Tensor::Layout::ANY);
    tensor.set_dims({1, 10});
    tensor.set_string("label", CLS_LABEL);
    tensor.set_int("label_id", CLS_LABEL_ID);
    tensor.set_double("confidence", CLS_CONFIDENCE);
    tensor.set_int("tensor_id", CLS_TENSOR_ID);
    tensor.set_type(GVA::GST_ANALYTICS_CLS_2_TENSOR);

    return s;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Keypoint: convert_to_meta tests
// ═══════════════════════════════════════════════════════════════════════════════

struct KeypointConvertToMetaTest : public ::testing::Test {
    GstBuffer *buffer = nullptr;
    GstAnalyticsRelationMeta *rmeta = nullptr;
    GstAnalyticsODMtd od_mtd = {};

    void SetUp() override {
        buffer = gst_buffer_new_allocate(nullptr, 0, nullptr);
        rmeta = gst_buffer_add_analytics_relation_meta(buffer);
        ASSERT_NE(rmeta, nullptr);

        GQuark label = g_quark_from_string("person");
        gboolean ret = gst_analytics_relation_meta_add_od_mtd(rmeta, label, OD_X, OD_Y, OD_W, OD_H, OD_CONF, &od_mtd);
        ASSERT_TRUE(ret);
    }

    void TearDown() override {
        if (buffer)
            gst_buffer_unref(buffer);
    }
};

TEST_F(KeypointConvertToMetaTest, ReturnsTrue) {
    GstStructure *s = build_keypoint_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    bool ok = tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);
    EXPECT_TRUE(ok);

    gst_structure_free(s);
}

TEST_F(KeypointConvertToMetaTest, GroupHasCorrectMemberCount) {
    GstStructure *s = build_keypoint_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

    gsize count = gst_analytics_group_mtd_get_member_count(&group_mtd);
    EXPECT_EQ(count, KEYPOINT_COUNT);

    gst_structure_free(s);
}

TEST_F(KeypointConvertToMetaTest, GroupHasSemanticTag) {
    GstStructure *s = build_keypoint_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

    // tag should be "model_name/format" = "Model0/body-pose/coco-17"
    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<const GstAnalyticsMtd *>(&group_mtd));
    ASSERT_NE(tag, nullptr);
    std::string expected = std::string("Model0/") + GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17;
    EXPECT_STREQ(tag, expected.c_str());
    g_free(tag);

    gst_structure_free(s);
}

TEST_F(KeypointConvertToMetaTest, PositionsArePixelCoords) {
    GstStructure *s = build_keypoint_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

    for (gsize k = 0; k < KEYPOINT_COUNT; k++) {
        GstAnalyticsMtd member;
        ASSERT_TRUE(gst_analytics_group_mtd_get_member(&group_mtd, k, &member));

        GstAnalyticsKeypointMtd kp_mtd;
        ASSERT_TRUE(gst_analytics_relation_meta_get_keypoint_mtd(rmeta, member.id, &kp_mtd));

        gint px, py, pz;
        GstAnalyticsKeypointDimensions dim;
        ASSERT_TRUE(gst_analytics_keypoint_mtd_get_position(&kp_mtd, &px, &py, &pz, &dim));

        gint expected_x = OD_X + static_cast<gint>(OD_W * KEYPOINT_POSITIONS_NORM[k * 2]);
        gint expected_y = OD_Y + static_cast<gint>(OD_H * KEYPOINT_POSITIONS_NORM[k * 2 + 1]);
        EXPECT_EQ(px, expected_x) << "keypoint " << k << " x mismatch";
        EXPECT_EQ(py, expected_y) << "keypoint " << k << " y mismatch";
    }

    gst_structure_free(s);
}

TEST_F(KeypointConvertToMetaTest, ConfidencesMatch) {
    GstStructure *s = build_keypoint_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

    for (gsize k = 0; k < KEYPOINT_COUNT; k++) {
        GstAnalyticsMtd member;
        ASSERT_TRUE(gst_analytics_group_mtd_get_member(&group_mtd, k, &member));

        GstAnalyticsKeypointMtd kp_mtd;
        ASSERT_TRUE(gst_analytics_relation_meta_get_keypoint_mtd(rmeta, member.id, &kp_mtd));

        gfloat conf;
        ASSERT_TRUE(gst_analytics_keypoint_mtd_get_confidence(&kp_mtd, &conf));
        EXPECT_FLOAT_EQ(conf, KEYPOINT_CONFIDENCES[k]) << "keypoint " << k << " confidence mismatch";
    }

    gst_structure_free(s);
}

TEST_F(KeypointConvertToMetaTest, UnknownTypeReturnsFalse) {
    GstStructure *s = gst_structure_new_empty("unknown");
    GVA::Tensor tensor(s);
    tensor.set_type("unsupported_type");

    GstAnalyticsMtd mtd = {};
    bool ok = tensor.convert_to_meta(&mtd, rmeta, OD_X, OD_Y, OD_W, OD_H);
    EXPECT_FALSE(ok);

    gst_structure_free(s);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Keypoint: convert_to_tensor tests
// ═══════════════════════════════════════════════════════════════════════════════

struct KeypointConvertToTensorTest : public ::testing::Test {
    GstBuffer *buffer = nullptr;
    GstAnalyticsRelationMeta *rmeta = nullptr;
    GstAnalyticsODMtd od_mtd = {};
    GstAnalyticsGroupMtd group_mtd = {};
    GstStructure *orig_s = nullptr;                   // original input tensor – kept for comparison
    std::vector<GstStructure *> roundtrip_structures; // track allocations from roundtrip_tensor()

    void SetUp() override {
        buffer = gst_buffer_new_allocate(nullptr, 0, nullptr);
        rmeta = gst_buffer_add_analytics_relation_meta(buffer);
        ASSERT_NE(rmeta, nullptr);

        GQuark label = g_quark_from_string("person");
        gboolean ret = gst_analytics_relation_meta_add_od_mtd(rmeta, label, OD_X, OD_Y, OD_W, OD_H, OD_CONF, &od_mtd);
        ASSERT_TRUE(ret);

        // Build tensor → convert to meta (keep original for comparison)
        orig_s = build_keypoint_structure();
        GVA::Tensor tensor(orig_s);
        tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

        // Set IS_PART_OF so convert_to_tensor finds the bbox
        gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_IS_PART_OF, group_mtd.id, od_mtd.id);
    }

    void TearDown() override {
        for (auto *s : roundtrip_structures)
            gst_structure_free(s);
        if (orig_s)
            gst_structure_free(orig_s);
        if (buffer)
            gst_buffer_unref(buffer);
    }

    GVA::Tensor original_tensor() {
        return GVA::Tensor(orig_s);
    }

    GVA::Tensor roundtrip_tensor() {
        GstStructure *result = GVA::Tensor::convert_to_tensor(*reinterpret_cast<GstAnalyticsMtd *>(&group_mtd));
        EXPECT_NE(result, nullptr);
        if (result)
            roundtrip_structures.push_back(result);
        return GVA::Tensor(result);
    }
};

TEST_F(KeypointConvertToTensorTest, NameIsKeypoints) {
    auto orig = original_tensor();
    auto t = roundtrip_tensor();
    EXPECT_EQ(t.name(), orig.name());
}

TEST_F(KeypointConvertToTensorTest, TypeIsKeypoints) {
    auto orig = original_tensor();
    auto t = roundtrip_tensor();
    EXPECT_EQ(t.type(), orig.type());
}

TEST_F(KeypointConvertToTensorTest, FormatIsSemanticTag) {
    auto orig = original_tensor();
    auto t = roundtrip_tensor();
    EXPECT_EQ(t.format(), orig.format());
}

TEST_F(KeypointConvertToTensorTest, PrecisionIsFP32) {
    auto orig = original_tensor();
    auto t = roundtrip_tensor();
    EXPECT_EQ(t.precision(), orig.precision());
}

TEST_F(KeypointConvertToTensorTest, DimsMatch) {
    auto orig = original_tensor();
    auto t = roundtrip_tensor();
    EXPECT_EQ(t.dims(), orig.dims());
}

TEST_F(KeypointConvertToTensorTest, DataRoundtripsPositions) {
    auto orig = original_tensor();
    auto t = roundtrip_tensor();
    auto orig_data = orig.data<float>();
    auto rest_data = t.data<float>();
    ASSERT_EQ(rest_data.size(), orig_data.size());

    for (size_t i = 0; i < orig_data.size(); i++) {
        EXPECT_NEAR(rest_data[i], orig_data[i], 0.01f) << "position[" << i << "] roundtrip mismatch";
    }
}

TEST_F(KeypointConvertToTensorTest, ConfidencesRoundtrip) {
    auto orig = original_tensor();
    auto t = roundtrip_tensor();
    auto orig_conf = orig.confidences();
    auto rest_conf = t.confidences();
    ASSERT_EQ(rest_conf.size(), orig_conf.size());

    for (size_t k = 0; k < orig_conf.size(); k++) {
        EXPECT_FLOAT_EQ(rest_conf[k], orig_conf[k]) << "keypoint " << k << " confidence roundtrip mismatch";
    }
}

TEST_F(KeypointConvertToTensorTest, PointNamesRoundtrip) {
    auto orig = original_tensor();
    auto t = roundtrip_tensor();
    EXPECT_EQ(t.get_vector<std::string>("point_names"), orig.get_vector<std::string>("point_names"));
}

TEST_F(KeypointConvertToTensorTest, SkeletonRoundtrip) {
    auto orig = original_tensor();
    auto t = roundtrip_tensor();
    auto orig_conn = orig.get_vector<uint32_t>("point_connections");
    auto rest_conn = t.get_vector<uint32_t>("point_connections");

    // Convert to set of pairs for order-independent comparison
    std::set<std::pair<uint32_t, uint32_t>> orig_pairs;
    for (size_t i = 0; i < orig_conn.size(); i += 2)
        orig_pairs.emplace(orig_conn[i], orig_conn[i + 1]);

    std::set<std::pair<uint32_t, uint32_t>> rest_pairs;
    ASSERT_EQ(rest_conn.size() % 2, 0u);
    for (size_t i = 0; i < rest_conn.size(); i += 2)
        rest_pairs.emplace(rest_conn[i], rest_conn[i + 1]);

    EXPECT_EQ(rest_pairs, orig_pairs);
}

TEST_F(KeypointConvertToTensorTest, EmptyGroupReturnsNullptr) {
    // Create a group with zero keypoint members
    GstAnalyticsGroupMtd empty_group = {};
    gst_analytics_relation_meta_add_group_mtd(rmeta, 0, &empty_group);

    GstStructure *result = GVA::Tensor::convert_to_tensor(*reinterpret_cast<GstAnalyticsMtd *>(&empty_group));
    EXPECT_EQ(result, nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Classification: convert_to_meta tests
// ═══════════════════════════════════════════════════════════════════════════════

struct ClassificationConvertToMetaTest : public ::testing::Test {
    GstBuffer *buffer = nullptr;
    GstAnalyticsRelationMeta *rmeta = nullptr;

    void SetUp() override {
        buffer = gst_buffer_new_allocate(nullptr, 0, nullptr);
        rmeta = gst_buffer_add_analytics_relation_meta(buffer);
        ASSERT_NE(rmeta, nullptr);
    }

    void TearDown() override {
        if (buffer)
            gst_buffer_unref(buffer);
    }
};

TEST_F(ClassificationConvertToMetaTest, ReturnsTrue) {
    GstStructure *s = build_classification_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsClsMtd cls_mtd = {};
    // od coordinates not needed for classification — use defaults
    bool ok = tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd), rmeta);
    EXPECT_TRUE(ok);

    gst_structure_free(s);
}

TEST_F(ClassificationConvertToMetaTest, ClsMtdLabel) {
    GstStructure *s = build_classification_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsClsMtd cls_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd), rmeta);

    gsize count = gst_analytics_cls_mtd_get_length(&cls_mtd);
    ASSERT_EQ(count, 1u);

    GQuark quark = gst_analytics_cls_mtd_get_quark(&cls_mtd, 0);
    EXPECT_STREQ(g_quark_to_string(quark), CLS_LABEL);

    gst_structure_free(s);
}

TEST_F(ClassificationConvertToMetaTest, ClsMtdConfidence) {
    GstStructure *s = build_classification_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsClsMtd cls_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd), rmeta);

    gfloat conf = gst_analytics_cls_mtd_get_level(&cls_mtd, 0);
    EXPECT_FLOAT_EQ(conf, static_cast<gfloat>(CLS_CONFIDENCE));

    gst_structure_free(s);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Classification: convert_to_tensor tests
// ═══════════════════════════════════════════════════════════════════════════════

struct ClassificationConvertToTensorTest : public ::testing::Test {
    GstBuffer *buffer = nullptr;
    GstAnalyticsRelationMeta *rmeta = nullptr;
    GstAnalyticsClsMtd cls_mtd = {};
    GstStructure *orig_s = nullptr;                   // original input tensor – kept for comparison
    std::vector<GstStructure *> roundtrip_structures; // track allocations from roundtrip_tensor()

    void SetUp() override {
        buffer = gst_buffer_new_allocate(nullptr, 0, nullptr);
        rmeta = gst_buffer_add_analytics_relation_meta(buffer);
        ASSERT_NE(rmeta, nullptr);

        orig_s = build_classification_structure();
        GVA::Tensor tensor(orig_s);
        tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd), rmeta);
    }

    void TearDown() override {
        for (auto *s : roundtrip_structures)
            gst_structure_free(s);
        if (orig_s)
            gst_structure_free(orig_s);
        if (buffer)
            gst_buffer_unref(buffer);
    }

    GVA::Tensor original_tensor() {
        return GVA::Tensor(orig_s);
    }

    GVA::Tensor roundtrip_tensor() {
        GstStructure *result = GVA::Tensor::convert_to_tensor(*reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd));
        EXPECT_NE(result, nullptr);
        if (result)
            roundtrip_structures.push_back(result);
        return GVA::Tensor(result);
    }
};

TEST_F(ClassificationConvertToTensorTest, NameIsClassification) {
    auto t = roundtrip_tensor();
    // Name changes: input "classification_layer_name:output" → output "classification"
    EXPECT_EQ(t.name(), "classification");
}

TEST_F(ClassificationConvertToTensorTest, TypeIsClassificationResult) {
    auto orig = original_tensor();
    auto t = roundtrip_tensor();
    EXPECT_EQ(t.type(), orig.type());
}

TEST_F(ClassificationConvertToTensorTest, LabelMatches) {
    auto orig = original_tensor();
    auto t = roundtrip_tensor();
    EXPECT_EQ(t.label(), orig.label());
}

TEST_F(ClassificationConvertToTensorTest, ConfidenceMatches) {
    auto orig = original_tensor();
    auto t = roundtrip_tensor();
    // confidence goes through: double → gfloat (in ClsMtd) → double (in tensor)
    EXPECT_NEAR(t.confidence(), orig.confidence(), 1e-6);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Full roundtrip: tensor → meta → tensor
// ═══════════════════════════════════════════════════════════════════════════════

struct KeypointFullRoundtripTest : public ::testing::Test {
    GstBuffer *buffer = nullptr;
    GstAnalyticsRelationMeta *rmeta = nullptr;
    GstAnalyticsODMtd od_mtd = {};

    void SetUp() override {
        buffer = gst_buffer_new_allocate(nullptr, 0, nullptr);
        rmeta = gst_buffer_add_analytics_relation_meta(buffer);
        ASSERT_NE(rmeta, nullptr);

        GQuark label = g_quark_from_string("person");
        gboolean ret = gst_analytics_relation_meta_add_od_mtd(rmeta, label, OD_X, OD_Y, OD_W, OD_H, OD_CONF, &od_mtd);
        ASSERT_TRUE(ret);
    }

    void TearDown() override {
        if (buffer)
            gst_buffer_unref(buffer);
    }
};

TEST_F(KeypointFullRoundtripTest, FullRoundtrip) {
    // Original tensor
    GstStructure *orig_s = build_keypoint_structure();
    GVA::Tensor original(orig_s);

    // tensor → meta
    GstAnalyticsGroupMtd group_mtd = {};
    ASSERT_TRUE(
        original.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H));

    // Set IS_PART_OF so convert_to_tensor can find the bbox
    gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_IS_PART_OF, group_mtd.id, od_mtd.id);

    // meta → tensor
    GstStructure *restored_s = GVA::Tensor::convert_to_tensor(*reinterpret_cast<GstAnalyticsMtd *>(&group_mtd));
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    // Verify preserved fields
    EXPECT_EQ(restored.type(), original.type());
    EXPECT_EQ(restored.format(), original.format());
    EXPECT_EQ(restored.precision(), original.precision());
    EXPECT_EQ(restored.name(), GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);

    auto orig_dims = original.dims();
    auto rest_dims = restored.dims();
    EXPECT_EQ(rest_dims, orig_dims);

    // Verify positions approximately match (lossy due to float→int→float)
    auto orig_data = original.data<float>();
    auto rest_data = restored.data<float>();
    ASSERT_EQ(orig_data.size(), rest_data.size());
    for (size_t i = 0; i < orig_data.size(); i++) {
        EXPECT_NEAR(rest_data[i], orig_data[i], 0.01f) << "position[" << i << "] roundtrip mismatch";
    }

    // Verify confidences match
    auto orig_conf = original.confidences();
    auto rest_conf = restored.confidences();
    ASSERT_EQ(orig_conf.size(), rest_conf.size());
    for (size_t i = 0; i < orig_conf.size(); i++) {
        EXPECT_FLOAT_EQ(rest_conf[i], orig_conf[i]) << "confidence[" << i << "] roundtrip mismatch";
    }

    // Verify skeleton connections match original (order may differ due to relation reconstruction)
    auto orig_conn = original.get_vector<uint32_t>("point_connections");
    auto rest_conn = restored.get_vector<uint32_t>("point_connections");

    std::set<std::pair<uint32_t, uint32_t>> orig_pairs, rest_pairs;
    for (size_t i = 0; i < orig_conn.size(); i += 2)
        orig_pairs.emplace(orig_conn[i], orig_conn[i + 1]);
    for (size_t i = 0; i < rest_conn.size(); i += 2)
        rest_pairs.emplace(rest_conn[i], rest_conn[i + 1]);
    EXPECT_EQ(rest_pairs, orig_pairs);

    // Verify point names match original
    auto orig_names = original.get_vector<std::string>("point_names");
    auto rest_names = restored.get_vector<std::string>("point_names");
    EXPECT_EQ(rest_names, orig_names);

    gst_structure_free(orig_s);
    gst_structure_free(restored_s);
}

// ═══════════════════════════════════════════════════════════════════════════════
// OpenPose-18 skeleton roundtrip (covers descriptors with from_idx > to_idx)
// ═══════════════════════════════════════════════════════════════════════════════

// OpenPose-18 has 18 keypoints; simplified normalized positions for testing
static const float OPENPOSE18_POSITIONS_NORM[] = {
    0.49f, 0.14f, // nose
    0.50f, 0.20f, // neck
    0.40f, 0.21f, // shoulder_r
    0.30f, 0.31f, // elbow_r
    0.25f, 0.41f, // wrist_r
    0.60f, 0.21f, // shoulder_l
    0.70f, 0.31f, // elbow_l
    0.75f, 0.41f, // wrist_l
    0.43f, 0.50f, // hip_r
    0.42f, 0.65f, // knee_r
    0.41f, 0.80f, // ankle_r
    0.57f, 0.50f, // hip_l
    0.58f, 0.65f, // knee_l
    0.59f, 0.80f, // ankle_l
    0.45f, 0.12f, // eye_r
    0.55f, 0.12f, // eye_l
    0.40f, 0.11f, // ear_r
    0.60f, 0.11f, // ear_l
};

static const float OPENPOSE18_CONFIDENCES[] = {
    0.95f, 0.93f, 0.91f, 0.88f, 0.85f, 0.92f, 0.89f, 0.86f, 0.90f,
    0.87f, 0.84f, 0.90f, 0.87f, 0.84f, 0.80f, 0.82f, 0.78f, 0.79f,
};

static constexpr gsize OPENPOSE18_COUNT = 18;

static GstStructure *build_openpose18_keypoint_structure() {
    const auto *desc = openpose18_descriptor();
    GstStructure *s = gst_structure_new_empty(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    GVA::Tensor tensor(s);

    tensor.set_type(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    tensor.set_format(desc->semantic_tag);
    tensor.set_dims({static_cast<guint>(OPENPOSE18_COUNT), static_cast<guint>(KEYPOINT_DIM)});
    tensor.set_data(OPENPOSE18_POSITIONS_NORM, OPENPOSE18_COUNT * KEYPOINT_DIM * sizeof(float));
    tensor.set_precision(GVA::Tensor::Precision::FP32);
    tensor.set_vector<float>("confidence",
                             std::vector<float>(OPENPOSE18_CONFIDENCES, OPENPOSE18_CONFIDENCES + OPENPOSE18_COUNT));
    tensor.set_vector<std::string>("point_names",
                                   std::vector<std::string>(desc->point_names, desc->point_names + desc->point_count));
    tensor.set_vector<uint32_t>(
        "point_connections", std::vector<uint32_t>(desc->skeleton_connections,
                                                   desc->skeleton_connections + desc->skeleton_connection_count * 2));

    return s;
}

TEST_F(KeypointFullRoundtripTest, OpenPose18SkeletonRoundtrip) {
    // OpenPose-18 has pairs like (5,2), (6,5), etc. where from_idx > to_idx.
    // This test verifies that directional RELATE_TO relations are correctly
    // reconstructed during convert_to_tensor.
    GstStructure *orig_s = build_openpose18_keypoint_structure();
    GVA::Tensor original(orig_s);

    // tensor → meta
    GstAnalyticsGroupMtd group_mtd = {};
    ASSERT_TRUE(
        original.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H));
    gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_IS_PART_OF, group_mtd.id, od_mtd.id);

    // meta → tensor
    GstStructure *restored_s = GVA::Tensor::convert_to_tensor(*reinterpret_cast<GstAnalyticsMtd *>(&group_mtd));
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    // Verify all 13 skeleton pairs survive the roundtrip
    auto orig_conn = original.get_vector<uint32_t>("point_connections");
    auto rest_conn = restored.get_vector<uint32_t>("point_connections");

    std::set<std::pair<uint32_t, uint32_t>> orig_pairs, rest_pairs;
    for (size_t i = 0; i < orig_conn.size(); i += 2)
        orig_pairs.emplace(orig_conn[i], orig_conn[i + 1]);
    for (size_t i = 0; i < rest_conn.size(); i += 2)
        rest_pairs.emplace(rest_conn[i], rest_conn[i + 1]);

    EXPECT_EQ(orig_pairs.size(), 13u) << "OpenPose-18 should have 13 skeleton connections";
    EXPECT_EQ(rest_pairs, orig_pairs) << "All skeleton pairs including reversed ones must survive roundtrip";

    gst_structure_free(orig_s);
    gst_structure_free(restored_s);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Confidence / keypoint_count scenarios
// ═══════════════════════════════════════════════════════════════════════════════

// Small dataset shared with Python tests: 3 keypoints, 2D
static constexpr gsize SMALL_KP_COUNT = 3;
static constexpr gsize SMALL_KP_DIM = 2;
static const float SMALL_KP_POSITIONS[] = {0.25f, 0.30f, 0.50f, 0.60f, 0.75f, 0.90f};
static const float SMALL_KP_CONFIDENCES[] = {0.9f, 0.8f, 0.7f};

// Helper: build a minimal keypoints tensor (no descriptor, no skeleton)
static GstStructure *build_small_keypoint_structure(const float *conf, gsize conf_count, bool set_scalar = false,
                                                    double scalar_val = 0.0) {
    GstStructure *s = gst_structure_new_empty(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    GVA::Tensor tensor(s);
    tensor.set_type(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    tensor.set_dims({static_cast<guint>(SMALL_KP_COUNT), static_cast<guint>(SMALL_KP_DIM)});
    tensor.set_data(SMALL_KP_POSITIONS, SMALL_KP_COUNT * SMALL_KP_DIM * sizeof(float));
    tensor.set_precision(GVA::Tensor::Precision::FP32);

    if (conf && conf_count > 0)
        tensor.set_vector<float>("confidence", std::vector<float>(conf, conf + conf_count));
    else if (set_scalar)
        tensor.set_confidence(scalar_val);
    // else: no confidence field at all

    return s;
}

struct ConfidenceScenarioTest : public ::testing::Test {
    GstBuffer *buffer = nullptr;
    GstAnalyticsRelationMeta *rmeta = nullptr;
    GstAnalyticsODMtd od_mtd = {};

    void SetUp() override {
        buffer = gst_buffer_new_allocate(nullptr, 0, nullptr);
        rmeta = gst_buffer_add_analytics_relation_meta(buffer);
        ASSERT_NE(rmeta, nullptr);

        GQuark label = g_quark_from_string("person");
        gboolean ret = gst_analytics_relation_meta_add_od_mtd(rmeta, label, OD_X, OD_Y, OD_W, OD_H, OD_CONF, &od_mtd);
        ASSERT_TRUE(ret);
    }

    void TearDown() override {
        if (buffer)
            gst_buffer_unref(buffer);
    }

    // convert_to_meta and read back confidences from keypoint members
    std::vector<gfloat> meta_confidences(GstStructure *s) {
        GVA::Tensor tensor(s);
        GstAnalyticsGroupMtd group_mtd = {};
        bool ok =
            tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);
        EXPECT_TRUE(ok);

        gsize count = gst_analytics_group_mtd_get_member_count(&group_mtd);
        std::vector<gfloat> confs(count);
        for (gsize k = 0; k < count; k++) {
            GstAnalyticsMtd member;
            EXPECT_TRUE(gst_analytics_group_mtd_get_member(&group_mtd, k, &member));
            GstAnalyticsKeypointMtd kp_mtd;
            EXPECT_TRUE(gst_analytics_relation_meta_get_keypoint_mtd(rmeta, member.id, &kp_mtd));
            gfloat c = 0.0f;
            gst_analytics_keypoint_mtd_get_confidence(&kp_mtd, &c);
            confs[k] = c;
        }
        return confs;
    }
};

// Scenario 1: confidence array size matches keypoint_count → values passed through
TEST_F(ConfidenceScenarioTest, MatchingConfidenceArray) {
    GstStructure *s = build_small_keypoint_structure(SMALL_KP_CONFIDENCES, SMALL_KP_COUNT);
    auto confs = meta_confidences(s);
    ASSERT_EQ(confs.size(), SMALL_KP_COUNT);
    for (gsize k = 0; k < SMALL_KP_COUNT; k++) {
        EXPECT_FLOAT_EQ(confs[k], SMALL_KP_CONFIDENCES[k]) << "keypoint " << k;
    }
    gst_structure_free(s);
}

// Scenario 2: confidence array size != keypoint_count → values ignored (default 1.0)
TEST_F(ConfidenceScenarioTest, MismatchedConfidenceArray) {
    // provide 2 confidences for 3 keypoints
    const float bad_conf[] = {0.9f, 0.8f};
    GstStructure *s = build_small_keypoint_structure(bad_conf, 2);
    auto confs = meta_confidences(s);
    ASSERT_EQ(confs.size(), SMALL_KP_COUNT);
    for (gsize k = 0; k < SMALL_KP_COUNT; k++) {
        EXPECT_FLOAT_EQ(confs[k], 1.0f) << "keypoint " << k << " should be 1.0 (default, confidence ignored)";
    }
    gst_structure_free(s);
}

// Scenario 3: no confidence field at all → default 1.0
TEST_F(ConfidenceScenarioTest, NoConfidenceField) {
    GstStructure *s = build_small_keypoint_structure(nullptr, 0);
    auto confs = meta_confidences(s);
    ASSERT_EQ(confs.size(), SMALL_KP_COUNT);
    for (gsize k = 0; k < SMALL_KP_COUNT; k++) {
        EXPECT_FLOAT_EQ(confs[k], 1.0f) << "keypoint " << k << " should be 1.0 (default, no confidence)";
    }
    gst_structure_free(s);
}

// Scenario 4: scalar confidence with multiple keypoints → confidences() returns [scalar],
// size 1 != 3 → ignored (default 1.0)
TEST_F(ConfidenceScenarioTest, ScalarConfidenceMultipleKeypoints) {
    GstStructure *s = build_small_keypoint_structure(nullptr, 0, true, 0.85);
    auto confs = meta_confidences(s);
    ASSERT_EQ(confs.size(), SMALL_KP_COUNT);
    for (gsize k = 0; k < SMALL_KP_COUNT; k++) {
        EXPECT_FLOAT_EQ(confs[k], 1.0f) << "keypoint " << k
                                        << " should be 1.0 (default, scalar conf ignored for 3 kps)";
    }
    gst_structure_free(s);
}

// Scenario 5: single keypoint with scalar confidence → confidences() returns [scalar],
// size 1 == keypoint_count 1 → valid
TEST_F(ConfidenceScenarioTest, SingleKeypointScalarConfidence) {
    // Build a 1-keypoint tensor with scalar confidence
    GstStructure *s = gst_structure_new_empty(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    GVA::Tensor tensor(s);
    tensor.set_type(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    tensor.set_dims({1u, 2u});
    const float pos[] = {0.5f, 0.5f};
    tensor.set_data(pos, sizeof(pos));
    tensor.set_precision(GVA::Tensor::Precision::FP32);
    tensor.set_confidence(0.95);

    GstAnalyticsGroupMtd group_mtd = {};
    bool ok = tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);
    EXPECT_TRUE(ok);

    gsize count = gst_analytics_group_mtd_get_member_count(&group_mtd);
    ASSERT_EQ(count, 1u);

    GstAnalyticsMtd member;
    ASSERT_TRUE(gst_analytics_group_mtd_get_member(&group_mtd, 0, &member));
    GstAnalyticsKeypointMtd kp_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_get_keypoint_mtd(rmeta, member.id, &kp_mtd));
    gfloat c = 0.0f;
    ASSERT_TRUE(gst_analytics_keypoint_mtd_get_confidence(&kp_mtd, &c));
    EXPECT_NEAR(c, 0.95f, 1e-5f);

    gst_structure_free(s);
}

// Scenario 6: roundtrip with matching confidences preserves values
TEST_F(ConfidenceScenarioTest, RoundtripMatchingConfidence) {
    GstStructure *s = build_small_keypoint_structure(SMALL_KP_CONFIDENCES, SMALL_KP_COUNT);
    GVA::Tensor original(s);

    GstAnalyticsGroupMtd group_mtd = {};
    ASSERT_TRUE(
        original.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H));
    gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_IS_PART_OF, group_mtd.id, od_mtd.id);

    GstStructure *restored_s = GVA::Tensor::convert_to_tensor(*reinterpret_cast<GstAnalyticsMtd *>(&group_mtd));
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    auto rest_conf = restored.confidences();
    ASSERT_EQ(rest_conf.size(), SMALL_KP_COUNT);
    for (gsize k = 0; k < SMALL_KP_COUNT; k++) {
        EXPECT_FLOAT_EQ(rest_conf[k], SMALL_KP_CONFIDENCES[k]) << "keypoint " << k;
    }

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

// Scenario 7: roundtrip with mismatched confidences → restored tensor has zero confidences
TEST_F(ConfidenceScenarioTest, RoundtripMismatchedConfidence) {
    const float bad_conf[] = {0.9f, 0.8f}; // 2 values for 3 keypoints
    GstStructure *s = build_small_keypoint_structure(bad_conf, 2);
    GVA::Tensor original(s);

    GstAnalyticsGroupMtd group_mtd = {};
    ASSERT_TRUE(
        original.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H));
    gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_IS_PART_OF, group_mtd.id, od_mtd.id);

    GstStructure *restored_s = GVA::Tensor::convert_to_tensor(*reinterpret_cast<GstAnalyticsMtd *>(&group_mtd));
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    auto rest_conf = restored.confidences();
    ASSERT_EQ(rest_conf.size(), SMALL_KP_COUNT);
    for (gsize k = 0; k < SMALL_KP_COUNT; k++) {
        EXPECT_FLOAT_EQ(rest_conf[k], 1.0f)
            << "keypoint " << k << " should be 1.0 (default) after mismatched roundtrip";
    }

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Semantic tag handling in convert_to_meta / convert_to_tensor
// ═══════════════════════════════════════════════════════════════════════════════

struct SemanticTagTest : public ::testing::Test {
    GstBuffer *buffer = nullptr;
    GstAnalyticsRelationMeta *rmeta = nullptr;
    GstAnalyticsODMtd od_mtd = {};

    void SetUp() override {
        buffer = gst_buffer_new_allocate(nullptr, 0, nullptr);
        rmeta = gst_buffer_add_analytics_relation_meta(buffer);
        ASSERT_NE(rmeta, nullptr);

        GQuark label = g_quark_from_string("person");
        gboolean ret = gst_analytics_relation_meta_add_od_mtd(rmeta, label, OD_X, OD_Y, OD_W, OD_H, OD_CONF, &od_mtd);
        ASSERT_TRUE(ret);
    }

    void TearDown() override {
        if (buffer)
            gst_buffer_unref(buffer);
    }

    // Helper: build keypoint tensor with specific model_name and format
    // Uses 17 keypoints when format is a valid descriptor (to match skeleton connections)
    GstStructure *build_kp_tensor(const char *model_name, const char *format) {
        bool use_full = (format != nullptr && gst_analytics_keypoint_descriptor_lookup(format) != nullptr);
        gsize kp_count = use_full ? KEYPOINT_COUNT : SMALL_KP_COUNT;
        gsize kp_dim = SMALL_KP_DIM;
        const float *positions = use_full ? KEYPOINT_POSITIONS_NORM : SMALL_KP_POSITIONS;
        const float *confs = use_full ? KEYPOINT_CONFIDENCES : SMALL_KP_CONFIDENCES;

        GstStructure *s = gst_structure_new_empty(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
        GVA::Tensor tensor(s);
        tensor.set_type(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
        tensor.set_dims({static_cast<guint>(kp_count), static_cast<guint>(kp_dim)});
        tensor.set_data(positions, kp_count * kp_dim * sizeof(float));
        tensor.set_precision(GVA::Tensor::Precision::FP32);
        tensor.set_vector<float>("confidence", std::vector<float>(confs, confs + kp_count));
        if (model_name)
            tensor.set_model_name(model_name);
        if (format)
            tensor.set_format(format);
        return s;
    }

    // Helper: convert tensor to meta and back, return the restored tensor structure
    GstStructure *roundtrip(GstStructure *s) {
        GVA::Tensor tensor(s);
        GstAnalyticsGroupMtd group_mtd = {};
        EXPECT_TRUE(
            tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H));
        gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_IS_PART_OF, group_mtd.id, od_mtd.id);
        return GVA::Tensor::convert_to_tensor(*reinterpret_cast<GstAnalyticsMtd *>(&group_mtd));
    }
};

// ─── convert_to_meta: semantic tag composition ───────────────────────────────

TEST_F(SemanticTagTest, ConvertToMeta_ModelNameAndFormat_CompoundTag) {
    GstStructure *s = build_kp_tensor("MyModel", GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<const GstAnalyticsMtd *>(&group_mtd));
    ASSERT_NE(tag, nullptr);
    std::string expected = std::string("MyModel/") + GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17;
    EXPECT_STREQ(tag, expected.c_str());
    g_free(tag);
    gst_structure_free(s);
}

TEST_F(SemanticTagTest, ConvertToMeta_OnlyFormat_FormatAsTag) {
    GstStructure *s = build_kp_tensor(nullptr, GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<const GstAnalyticsMtd *>(&group_mtd));
    ASSERT_NE(tag, nullptr);
    EXPECT_STREQ(tag, GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    g_free(tag);
    gst_structure_free(s);
}

TEST_F(SemanticTagTest, ConvertToMeta_OnlyModelName_ModelNameAsTag) {
    GstStructure *s = build_kp_tensor("SomeModel", nullptr);
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<const GstAnalyticsMtd *>(&group_mtd));
    ASSERT_NE(tag, nullptr);
    EXPECT_STREQ(tag, "SomeModel");
    g_free(tag);
    gst_structure_free(s);
}

TEST_F(SemanticTagTest, ConvertToMeta_NoModelNameNoFormat_EmptyTag) {
    GstStructure *s = build_kp_tensor(nullptr, nullptr);
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<const GstAnalyticsMtd *>(&group_mtd));
    // empty tag: either null or empty string
    bool is_empty = (tag == nullptr || tag[0] == '\0');
    EXPECT_TRUE(is_empty);
    g_free(tag);
    gst_structure_free(s);
}

// ─── convert_to_tensor: semantic_tag field and format extraction ─────────────

TEST_F(SemanticTagTest, ConvertToTensor_CompoundTag_HasSemanticTagField) {
    GstStructure *s = build_kp_tensor("MyModel", GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    GstStructure *restored_s = roundtrip(s);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    // semantic_tag field should contain the full compound tag
    std::string expected = std::string("MyModel/") + GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17;
    EXPECT_EQ(restored.get_string("semantic_tag"), expected);

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

TEST_F(SemanticTagTest, ConvertToTensor_CompoundTag_FormatExtractedFromTag) {
    GstStructure *s = build_kp_tensor("MyModel", GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    GstStructure *restored_s = roundtrip(s);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    // format should be extracted as everything after first '/' = "body-pose/coco-17"
    EXPECT_EQ(restored.format(), GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

TEST_F(SemanticTagTest, ConvertToTensor_OnlyFormat_SemanticTagIsFormat) {
    GstStructure *s = build_kp_tensor(nullptr, GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    GstStructure *restored_s = roundtrip(s);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    // semantic_tag = format (no model_name prefix)
    EXPECT_EQ(restored.get_string("semantic_tag"), GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    // format extracted: full tag "body-pose/coco-17" is a valid descriptor, so it's used directly
    EXPECT_EQ(restored.format(), GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

TEST_F(SemanticTagTest, ConvertToTensor_OnlyModelName_SemanticTagIsModelName) {
    GstStructure *s = build_kp_tensor("SomeModel", nullptr);
    GstStructure *restored_s = roundtrip(s);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    // semantic_tag = model_name
    EXPECT_EQ(restored.get_string("semantic_tag"), "SomeModel");
    // format should NOT be set (no valid descriptor for "SomeModel")
    EXPECT_EQ(restored.format(), "");

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

TEST_F(SemanticTagTest, ConvertToTensor_NoTag_NoSemanticTagField) {
    GstStructure *s = build_kp_tensor(nullptr, nullptr);
    GstStructure *restored_s = roundtrip(s);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    // no semantic_tag field
    EXPECT_FALSE(restored.has_field("semantic_tag"));
    // no format either
    EXPECT_EQ(restored.format(), "");

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

TEST_F(SemanticTagTest, ConvertToTensor_CompoundTag_DescriptorUsedForPointNames) {
    // With compound tag "MyModel/body-pose/coco-17", format extracted is "body-pose/coco-17"
    // which should match the coco-17 descriptor and provide point_names
    const auto *desc = coco17_descriptor();

    // Build a 17-keypoint tensor with model_name and valid format
    GstStructure *s = gst_structure_new_empty(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    GVA::Tensor tensor(s);
    tensor.set_type(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    tensor.set_model_name("MyModel");
    tensor.set_format(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    tensor.set_dims({static_cast<guint>(KEYPOINT_COUNT), static_cast<guint>(KEYPOINT_DIM)});
    tensor.set_data(KEYPOINT_POSITIONS_NORM, KEYPOINT_COUNT * KEYPOINT_DIM * sizeof(float));
    tensor.set_precision(GVA::Tensor::Precision::FP32);
    tensor.set_vector<float>("confidence",
                             std::vector<float>(KEYPOINT_CONFIDENCES, KEYPOINT_CONFIDENCES + KEYPOINT_COUNT));

    GstStructure *restored_s = roundtrip(s);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    // point_names should come from descriptor
    auto names = restored.get_vector<std::string>("point_names");
    ASSERT_EQ(names.size(), desc->point_count);
    for (gsize k = 0; k < desc->point_count; k++) {
        EXPECT_EQ(names[k], desc->point_names[k]) << "point_name[" << k << "] mismatch";
    }

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

TEST_F(SemanticTagTest, ConvertToTensor_InvalidFormat_NoFormatFieldSet) {
    // If model_name/format doesn't result in a valid descriptor, format should not be set
    GstStructure *s = build_kp_tensor("MyModel", "unknown-format");
    GstStructure *restored_s = roundtrip(s);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    // semantic_tag should be stored
    EXPECT_EQ(restored.get_string("semantic_tag"), "MyModel/unknown-format");
    // format should NOT be set because descriptor lookup for "unknown-format" fails
    EXPECT_EQ(restored.format(), "");

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

// ─── Classification semantic tag ─────────────────────────────────────────────

TEST_F(SemanticTagTest, Cls_ConvertToMeta_SetsModelNameAsTag) {
    GstStructure *s = build_classification_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsClsMtd cls_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd), rmeta);

    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<const GstAnalyticsMtd *>(&cls_mtd));
    ASSERT_NE(tag, nullptr);
    // model_name = "torch_jit" (from build_classification_structure)
    EXPECT_STREQ(tag, "torch_jit");
    g_free(tag);
    gst_structure_free(s);
}

TEST_F(SemanticTagTest, Cls_ConvertToTensor_HasSemanticTagField) {
    GstStructure *s = build_classification_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsClsMtd cls_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd), rmeta);

    GstStructure *restored_s = GVA::Tensor::convert_to_tensor(*reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd));
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    EXPECT_EQ(restored.get_string("semantic_tag"), "torch_jit");

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

TEST_F(SemanticTagTest, Cls_NoModelName_NoSemanticTag) {
    // Build cls tensor without model_name
    GstStructure *s = gst_structure_new_empty("classification");
    GVA::Tensor tensor(s);
    tensor.set_type(GVA::GST_ANALYTICS_CLS_2_TENSOR);
    tensor.set_string("label", "cat");
    tensor.set_double("confidence", 0.9);

    GstAnalyticsClsMtd cls_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd), rmeta);

    // no model_name → no semantic tag set
    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<const GstAnalyticsMtd *>(&cls_mtd));
    EXPECT_TRUE(tag == nullptr || tag[0] == '\0');
    g_free(tag);

    // roundtrip: no semantic_tag field expected
    GstStructure *restored_s = GVA::Tensor::convert_to_tensor(*reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd));
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);
    EXPECT_EQ(restored.get_string("semantic_tag"), "");

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

TEST_F(SemanticTagTest, Cls_EmptyModelName_NoSemanticTag) {
    // model_name explicitly set to "" should be treated as absent
    GstStructure *s = gst_structure_new_empty("classification");
    GVA::Tensor tensor(s);
    tensor.set_type(GVA::GST_ANALYTICS_CLS_2_TENSOR);
    tensor.set_string("label", "dog");
    tensor.set_double("confidence", 0.85);
    tensor.set_model_name("");

    GstAnalyticsClsMtd cls_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd), rmeta);

    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<const GstAnalyticsMtd *>(&cls_mtd));
    EXPECT_TRUE(tag == nullptr || tag[0] == '\0');
    g_free(tag);
    gst_structure_free(s);
}

// ─── Group: edge cases ───────────────────────────────────────────────────────

TEST_F(SemanticTagTest, ConvertToMeta_EmptyModelName_OnlyFormat) {
    // model_name explicitly "" + valid format → tag is just format
    GstStructure *s = build_kp_tensor(nullptr, GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    GVA::Tensor tensor(s);
    tensor.set_model_name(""); // explicitly empty

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<const GstAnalyticsMtd *>(&group_mtd));
    ASSERT_NE(tag, nullptr);
    EXPECT_STREQ(tag, GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    g_free(tag);
    gst_structure_free(s);
}

TEST_F(SemanticTagTest, ConvertToMeta_ModelNameWithSlashes_CompoundTag) {
    // model_name contains slashes: "pipeline/pose-model"
    GstStructure *s = build_kp_tensor("pipeline/pose-model", GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<const GstAnalyticsMtd *>(&group_mtd));
    ASSERT_NE(tag, nullptr);
    EXPECT_STREQ(tag, "pipeline/pose-model/body-pose/coco-17");
    g_free(tag);
    gst_structure_free(s);
}

TEST_F(SemanticTagTest, ConvertToTensor_ModelNameWithSlashes_FormatExtracted) {
    // Roundtrip with multi-slash prefix: find_in_tag should still find the descriptor suffix
    GstStructure *s = build_kp_tensor("pipeline/pose-model", GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    GstStructure *restored_s = roundtrip(s);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    EXPECT_EQ(restored.get_string("semantic_tag"), "pipeline/pose-model/body-pose/coco-17");
    EXPECT_EQ(restored.format(), GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

TEST_F(SemanticTagTest, ConvertToMeta_InvalidFormat_TagStillComposed) {
    // model_name + unknown format → tag = "Model/custom-fmt" (tag is still set)
    GstStructure *s = build_kp_tensor("Model", "custom-fmt");
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<const GstAnalyticsMtd *>(&group_mtd));
    ASSERT_NE(tag, nullptr);
    EXPECT_STREQ(tag, "Model/custom-fmt");
    g_free(tag);
    gst_structure_free(s);
}

TEST_F(SemanticTagTest, ConvertToMeta_InvalidFormatOnly_TagIsFormat) {
    // no model_name + unknown format → tag is just the format string
    GstStructure *s = build_kp_tensor(nullptr, "my-custom-format");
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), rmeta, OD_X, OD_Y, OD_W, OD_H);

    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<const GstAnalyticsMtd *>(&group_mtd));
    ASSERT_NE(tag, nullptr);
    EXPECT_STREQ(tag, "my-custom-format");
    g_free(tag);
    gst_structure_free(s);
}

TEST_F(SemanticTagTest, ConvertToTensor_InvalidFormatOnly_SemanticTagStoredNoFormat) {
    // Roundtrip: unknown format without model_name → semantic_tag stored, format empty
    GstStructure *s = build_kp_tensor(nullptr, "my-custom-format");
    GstStructure *restored_s = roundtrip(s);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    EXPECT_EQ(restored.get_string("semantic_tag"), "my-custom-format");
    EXPECT_EQ(restored.format(), "");

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

TEST_F(SemanticTagTest, ConvertToTensor_ModelNameNotRestoredFromTag) {
    // Verify that model_name is NOT auto-reconstructed from semantic_tag
    GstStructure *s = build_kp_tensor("OrigModel", GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    GstStructure *restored_s = roundtrip(s);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    // model_name should not be set on the restored tensor
    EXPECT_EQ(restored.model_name(), "");
    // but semantic_tag contains the full info
    std::string expected = std::string("OrigModel/") + GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17;
    EXPECT_EQ(restored.get_string("semantic_tag"), expected);

    gst_structure_free(s);
    gst_structure_free(restored_s);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Segmentation: test data and helpers
// ═══════════════════════════════════════════════════════════════════════════════

// Semantic segmentation: frame-level int64 class-index map, dims = [1, H, W]
static constexpr guint SEG_SEM_W = 4;
static constexpr guint SEG_SEM_H = 3;
static const int64_t SEG_SEM_DATA[SEG_SEM_W * SEG_SEM_H] = {
    0, 0, 1, 1, //
    0, 2, 2, 1, //
    0, 0, 2, 2, //
};

// Frame dimensions used as the reference rectangle for semantic segmentation
static constexpr gint SEG_FRAME_W = 64;
static constexpr gint SEG_FRAME_H = 48;

// Instance segmentation: per-ROI integer (already thresholded) mask, dims = [W, H]
static constexpr guint SEG_INST_W = 3;
static constexpr guint SEG_INST_H = 2;
// Binary region-id mask produced by the converter (foreground = 1, background = 0)
static const uint8_t SEG_INST_MASK[SEG_INST_W * SEG_INST_H] = {
    0, 1, 1, //
    0, 1, 0, //
};

// Bounding box used as the reference rectangle for instance segmentation
static constexpr gint SEG_BOX_X = 10;
static constexpr gint SEG_BOX_Y = 20;
static constexpr gint SEG_BOX_W = 30;
static constexpr gint SEG_BOX_H = 40;

static GstStructure *build_semantic_segmentation_structure(const char *model_name = "SegModel") {
    GstStructure *s = gst_structure_new_empty(GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR);
    GVA::Tensor tensor(s);
    tensor.set_type(GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR);
    tensor.set_format("semantic_segmentation");
    tensor.set_model_name(model_name);
    tensor.set_precision(GVA::Tensor::Precision::I64);
    tensor.set_dims({1u, SEG_SEM_H, SEG_SEM_W});
    tensor.set_data(SEG_SEM_DATA, sizeof(SEG_SEM_DATA));
    return s;
}

static GstStructure *build_instance_segmentation_structure(const char *model_name = "SegModel") {
    GstStructure *s = gst_structure_new_empty(GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR);
    GVA::Tensor tensor(s);
    tensor.set_type(GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR);
    tensor.set_format("segmentation_mask");
    tensor.set_model_name(model_name);
    tensor.set_precision(GVA::Tensor::Precision::U8);
    tensor.set_dims({SEG_INST_W, SEG_INST_H});
    tensor.set_data(SEG_INST_MASK, sizeof(SEG_INST_MASK));
    return s;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Segmentation: convert_to_meta tests
// ═══════════════════════════════════════════════════════════════════════════════

struct SegmentationConvertToMetaTest : public ::testing::Test {
    GstBuffer *buffer = nullptr;
    GstAnalyticsRelationMeta *rmeta = nullptr;

    void SetUp() override {
        buffer = gst_buffer_new_allocate(nullptr, 0, nullptr);
        rmeta = gst_buffer_add_analytics_relation_meta(buffer);
        ASSERT_NE(rmeta, nullptr);
    }

    void TearDown() override {
        if (buffer)
            gst_buffer_unref(buffer);
    }
};

TEST_F(SegmentationConvertToMetaTest, SemanticReturnsTrue) {
    GstStructure *s = build_semantic_segmentation_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsSegmentationMtd seg_mtd = {};
    bool ok =
        tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&seg_mtd), rmeta, 0, 0, SEG_FRAME_W, SEG_FRAME_H);
    EXPECT_TRUE(ok);

    gst_structure_free(s);
}

TEST_F(SegmentationConvertToMetaTest, SemanticMaskHasCorrectVideoMeta) {
    GstStructure *s = build_semantic_segmentation_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsSegmentationMtd seg_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&seg_mtd), rmeta, 0, 0, SEG_FRAME_W, SEG_FRAME_H);

    gint loc_x = -1, loc_y = -1;
    guint loc_w = 0, loc_h = 0;
    GstBuffer *mask = gst_analytics_segmentation_mtd_get_mask(&seg_mtd, &loc_x, &loc_y, &loc_w, &loc_h);
    ASSERT_NE(mask, nullptr);

    // masks_loc should equal the frame reference rectangle
    EXPECT_EQ(loc_x, 0);
    EXPECT_EQ(loc_y, 0);
    EXPECT_EQ(loc_w, static_cast<guint>(SEG_FRAME_W));
    EXPECT_EQ(loc_h, static_cast<guint>(SEG_FRAME_H));

    GstVideoMeta *vmeta = gst_buffer_get_video_meta(mask);
    ASSERT_NE(vmeta, nullptr);
    EXPECT_EQ(vmeta->format, GST_VIDEO_FORMAT_GRAY8);
    EXPECT_EQ(vmeta->width, SEG_SEM_W);
    EXPECT_EQ(vmeta->height, SEG_SEM_H);

    gst_buffer_unref(mask);
    gst_structure_free(s);
}

TEST_F(SegmentationConvertToMetaTest, SemanticRegionCountMatchesUniqueClasses) {
    GstStructure *s = build_semantic_segmentation_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsSegmentationMtd seg_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&seg_mtd), rmeta, 0, 0, SEG_FRAME_W, SEG_FRAME_H);

    // unique class ids in SEG_SEM_DATA are {0, 1, 2}
    gsize count = gst_analytics_segmentation_mtd_get_region_count(&seg_mtd);
    EXPECT_EQ(count, 3u);

    gst_structure_free(s);
}

TEST_F(SegmentationConvertToMetaTest, InstanceCreatesTensorMtd) {
    // Instance segmentation is stored as a GstAnalyticsTensorMtd (FP32 soft mask), tagged so it can be
    // distinguished from other raw tensors. convert_to_meta must succeed and produce a tensor mtd.
    GstStructure *s = build_instance_segmentation_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsMtd mtd = {};
    bool ok = tensor.convert_to_meta(&mtd, rmeta, SEG_BOX_X, SEG_BOX_Y, SEG_BOX_W, SEG_BOX_H);
    EXPECT_TRUE(ok);
    EXPECT_EQ(gst_analytics_mtd_get_mtd_type(&mtd), gst_analytics_tensor_mtd_get_mtd_type());

    gchar *tag = gst_analytics_mtd_get_semantic_tag(&mtd);
    ASSERT_NE(tag, nullptr);
    EXPECT_NE(std::string(tag).find("instance_segmentation"), std::string::npos);
    g_free(tag);

    gst_structure_free(s);
}

TEST_F(SegmentationConvertToMetaTest, InstanceFloatMaskCreatesTensorMtd) {
    // A soft float mask is the expected instance-segmentation input; it must create a tensor mtd.
    static const float float_mask[SEG_INST_W * SEG_INST_H] = {0.1f, 0.7f, 0.9f, 0.4f, 0.6f, 0.2f};
    GstStructure *s = gst_structure_new_empty(GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR);
    {
        GVA::Tensor tensor(s);
        tensor.set_type(GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR);
        tensor.set_format("segmentation_mask");
        tensor.set_precision(GVA::Tensor::Precision::FP32);
        tensor.set_dims({SEG_INST_W, SEG_INST_H});
        tensor.set_data(float_mask, sizeof(float_mask));
    }
    GVA::Tensor tensor(s);

    GstAnalyticsMtd mtd = {};
    bool ok = tensor.convert_to_meta(&mtd, rmeta, SEG_BOX_X, SEG_BOX_Y, SEG_BOX_W, SEG_BOX_H);
    EXPECT_TRUE(ok);
    EXPECT_EQ(gst_analytics_mtd_get_mtd_type(&mtd), gst_analytics_tensor_mtd_get_mtd_type());

    gst_structure_free(s);
}

TEST_F(SegmentationConvertToMetaTest, InstanceTensorMtdRoundtrip) {
    // tensor → tensor mtd → tensor must preserve the soft FP32 mask, format, dims and model name.
    static const float float_mask[SEG_INST_W * SEG_INST_H] = {0.1f, 0.7f, 0.9f, 0.4f, 0.6f, 0.2f};
    GstStructure *s = gst_structure_new_empty(GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR);
    {
        GVA::Tensor t(s);
        t.set_type(GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR);
        t.set_format("segmentation_mask");
        t.set_model_name("SegModel");
        t.set_precision(GVA::Tensor::Precision::FP32);
        t.set_dims({SEG_INST_W, SEG_INST_H});
        t.set_data(float_mask, sizeof(float_mask));
    }
    GVA::Tensor tensor(s);

    GstAnalyticsMtd mtd = {};
    ASSERT_TRUE(tensor.convert_to_meta(&mtd, rmeta, SEG_BOX_X, SEG_BOX_Y, SEG_BOX_W, SEG_BOX_H));

    GstStructure *restored_s = GVA::Tensor::convert_to_tensor(mtd);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    EXPECT_EQ(restored.format(), "segmentation_mask");
    EXPECT_EQ(restored.precision(), GVA::Tensor::Precision::FP32);
    std::vector<guint> expected_dims = {SEG_INST_W, SEG_INST_H};
    EXPECT_EQ(restored.dims(), expected_dims);
    EXPECT_EQ(restored.model_name(), "SegModel");

    auto restored_data = restored.data<float>();
    ASSERT_EQ(restored_data.size(), static_cast<size_t>(SEG_INST_W) * SEG_INST_H);
    for (size_t i = 0; i < restored_data.size(); i++)
        EXPECT_NEAR(restored_data[i], float_mask[i], 1e-6f) << "instance mask pixel[" << i << "] mismatch";

    gst_structure_free(restored_s);
    gst_structure_free(s);
}

TEST_F(SegmentationConvertToMetaTest, UnknownTypeReturnsFalse) {
    GstStructure *s = gst_structure_new_empty("unknown");
    GVA::Tensor tensor(s);
    tensor.set_type("unsupported_type");

    GstAnalyticsMtd mtd = {};
    bool ok = tensor.convert_to_meta(&mtd, rmeta, 0, 0, SEG_FRAME_W, SEG_FRAME_H);
    EXPECT_FALSE(ok);

    gst_structure_free(s);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Segmentation: convert_to_tensor tests
// ═══════════════════════════════════════════════════════════════════════════════

struct SegmentationConvertToTensorTest : public ::testing::Test {
    GstBuffer *buffer = nullptr;
    GstAnalyticsRelationMeta *rmeta = nullptr;
    std::vector<GstStructure *> roundtrip_structures;

    void SetUp() override {
        buffer = gst_buffer_new_allocate(nullptr, 0, nullptr);
        rmeta = gst_buffer_add_analytics_relation_meta(buffer);
        ASSERT_NE(rmeta, nullptr);
    }

    void TearDown() override {
        for (auto *s : roundtrip_structures)
            gst_structure_free(s);
        if (buffer)
            gst_buffer_unref(buffer);
    }

    GstStructure *roundtrip(GstAnalyticsSegmentationMtd *seg_mtd) {
        GstStructure *result = GVA::Tensor::convert_to_tensor(*reinterpret_cast<GstAnalyticsMtd *>(seg_mtd));
        if (result)
            roundtrip_structures.push_back(result);
        return result;
    }
};

TEST_F(SegmentationConvertToTensorTest, SemanticRoundtrip) {
    GstStructure *orig_s = build_semantic_segmentation_structure();
    GVA::Tensor original(orig_s);

    GstAnalyticsSegmentationMtd seg_mtd = {};
    ASSERT_TRUE(
        original.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&seg_mtd), rmeta, 0, 0, SEG_FRAME_W, SEG_FRAME_H));

    GstStructure *restored_s = roundtrip(&seg_mtd);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    EXPECT_EQ(restored.type(), GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR);
    EXPECT_EQ(restored.format(), "semantic_segmentation");
    EXPECT_EQ(restored.precision(), GVA::Tensor::Precision::I64);
    EXPECT_EQ(restored.model_name(), "SegModel");

    std::vector<guint> expected_dims = {1u, SEG_SEM_H, SEG_SEM_W};
    EXPECT_EQ(restored.dims(), expected_dims);

    auto restored_data = restored.data<int64_t>();
    ASSERT_EQ(restored_data.size(), static_cast<size_t>(SEG_SEM_W) * SEG_SEM_H);
    for (size_t i = 0; i < restored_data.size(); i++) {
        EXPECT_EQ(restored_data[i], SEG_SEM_DATA[i]) << "semantic mask pixel[" << i << "] mismatch";
    }

    gst_structure_free(orig_s);
}

TEST_F(SegmentationConvertToTensorTest, SemanticGray16Roundtrip) {
    // class ids exceeding 255 force a GRAY16_LE mask; verify the values survive
    GstStructure *s = gst_structure_new_empty(GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR);
    GVA::Tensor tensor(s);
    tensor.set_type(GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR);
    tensor.set_format("semantic_segmentation");
    tensor.set_precision(GVA::Tensor::Precision::I64);
    const guint w = 2, h = 2;
    const int64_t data[w * h] = {0, 300, 1000, 42};
    tensor.set_dims({1u, h, w});
    tensor.set_data(data, sizeof(data));

    GstAnalyticsSegmentationMtd seg_mtd = {};
    ASSERT_TRUE(
        tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&seg_mtd), rmeta, 0, 0, SEG_FRAME_W, SEG_FRAME_H));

    gint lx, ly;
    guint lw, lh;
    GstBuffer *mask = gst_analytics_segmentation_mtd_get_mask(&seg_mtd, &lx, &ly, &lw, &lh);
    ASSERT_NE(mask, nullptr);
    GstVideoMeta *vmeta = gst_buffer_get_video_meta(mask);
    ASSERT_NE(vmeta, nullptr);
    EXPECT_EQ(vmeta->format, GST_VIDEO_FORMAT_GRAY16_LE);
    gst_buffer_unref(mask);

    GstStructure *restored_s = roundtrip(&seg_mtd);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);
    auto restored_data = restored.data<int64_t>();
    ASSERT_EQ(restored_data.size(), static_cast<size_t>(w) * h);
    for (size_t i = 0; i < restored_data.size(); i++) {
        EXPECT_EQ(restored_data[i], data[i]) << "gray16 pixel[" << i << "] mismatch";
    }

    gst_structure_free(s);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Segmentation: full roundtrip tensor → meta → tensor
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(SegmentationConvertToTensorTest, SemanticFullRoundtripPreservesFields) {
    GstStructure *orig_s = build_semantic_segmentation_structure("MyModel");
    GVA::Tensor original(orig_s);

    GstAnalyticsSegmentationMtd seg_mtd = {};
    ASSERT_TRUE(
        original.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&seg_mtd), rmeta, 0, 0, SEG_FRAME_W, SEG_FRAME_H));

    GstStructure *restored_s = roundtrip(&seg_mtd);
    ASSERT_NE(restored_s, nullptr);
    GVA::Tensor restored(restored_s);

    EXPECT_EQ(restored.type(), original.type());
    EXPECT_EQ(restored.format(), original.format());
    EXPECT_EQ(restored.precision(), original.precision());
    EXPECT_EQ(restored.dims(), original.dims());
    // semantic_tag should encode "model_name/format"
    EXPECT_EQ(restored.get_string("semantic_tag"), "MyModel/semantic_segmentation");

    gst_structure_free(orig_s);
}
