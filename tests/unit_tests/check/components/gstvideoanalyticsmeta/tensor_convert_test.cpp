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
#include <gst/gstbuffer.h>
#include <gtest/gtest.h>

#include "dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h"
#include "dlstreamer/gst/metadata/gstanalyticskeypointmtd.h"

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
    GstStructure *s = gst_structure_new_empty(GVA::TENSOR_TYPE_KEYPOINTS);
    GVA::Tensor tensor(s);

    tensor.set_double("iou_threshold", 0.7);
    tensor.set_string("converter", "yolo_v11_pose");
    tensor.set_double("confidence_threshold", 0.5);
    tensor.set_string("layer_name", "output");
    tensor.set_string("model_name", "Model0");
    tensor.set_type(GVA::TENSOR_TYPE_KEYPOINTS);
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
    tensor.set_type(GVA::TENSOR_TYPE_CLASSIFICATION);

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
    bool ok = tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), &od_mtd, rmeta);
    EXPECT_TRUE(ok);

    gst_structure_free(s);
}

TEST_F(KeypointConvertToMetaTest, GroupHasCorrectMemberCount) {
    GstStructure *s = build_keypoint_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), &od_mtd, rmeta);

    gsize count = gst_analytics_group_mtd_get_member_count(&group_mtd);
    EXPECT_EQ(count, KEYPOINT_COUNT);

    gst_structure_free(s);
}

TEST_F(KeypointConvertToMetaTest, GroupHasSemanticTag) {
    GstStructure *s = build_keypoint_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), &od_mtd, rmeta);

    gchar *tag = gst_analytics_group_mtd_get_semantic_tag(&group_mtd);
    ASSERT_NE(tag, nullptr);
    EXPECT_STREQ(tag, GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    g_free(tag);

    gst_structure_free(s);
}

TEST_F(KeypointConvertToMetaTest, PositionsArePixelCoords) {
    GstStructure *s = build_keypoint_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsGroupMtd group_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), &od_mtd, rmeta);

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
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), &od_mtd, rmeta);

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
    bool ok = tensor.convert_to_meta(&mtd, &od_mtd, rmeta);
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
    GstStructure *orig_s = nullptr; // original input tensor – kept for comparison

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
        tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), &od_mtd, rmeta);

        // Set IS_PART_OF so convert_to_tensor finds the bbox
        gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_IS_PART_OF, group_mtd.id, od_mtd.id);
    }

    void TearDown() override {
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
    // od_mtd not needed for classification — pass nullptr
    bool ok = tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd), nullptr, rmeta);
    EXPECT_TRUE(ok);

    gst_structure_free(s);
}

TEST_F(ClassificationConvertToMetaTest, ClsMtdLabel) {
    GstStructure *s = build_classification_structure();
    GVA::Tensor tensor(s);

    GstAnalyticsClsMtd cls_mtd = {};
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd), nullptr, rmeta);

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
    tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd), nullptr, rmeta);

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
    GstStructure *orig_s = nullptr; // original input tensor – kept for comparison

    void SetUp() override {
        buffer = gst_buffer_new_allocate(nullptr, 0, nullptr);
        rmeta = gst_buffer_add_analytics_relation_meta(buffer);
        ASSERT_NE(rmeta, nullptr);

        orig_s = build_classification_structure();
        GVA::Tensor tensor(orig_s);
        tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&cls_mtd), nullptr, rmeta);
    }

    void TearDown() override {
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
    ASSERT_TRUE(original.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), &od_mtd, rmeta));

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
    EXPECT_EQ(restored.name(), GVA::TENSOR_TYPE_KEYPOINTS);

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
    GstStructure *s = gst_structure_new_empty(GVA::TENSOR_TYPE_KEYPOINTS);
    GVA::Tensor tensor(s);

    tensor.set_type(GVA::TENSOR_TYPE_KEYPOINTS);
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
    ASSERT_TRUE(original.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), &od_mtd, rmeta));
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
    GstStructure *s = gst_structure_new_empty(GVA::TENSOR_TYPE_KEYPOINTS);
    GVA::Tensor tensor(s);
    tensor.set_type(GVA::TENSOR_TYPE_KEYPOINTS);
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
        bool ok = tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), &od_mtd, rmeta);
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
    GstStructure *s = gst_structure_new_empty(GVA::TENSOR_TYPE_KEYPOINTS);
    GVA::Tensor tensor(s);
    tensor.set_type(GVA::TENSOR_TYPE_KEYPOINTS);
    tensor.set_dims({1u, 2u});
    const float pos[] = {0.5f, 0.5f};
    tensor.set_data(pos, sizeof(pos));
    tensor.set_precision(GVA::Tensor::Precision::FP32);
    tensor.set_confidence(0.95);

    GstAnalyticsGroupMtd group_mtd = {};
    bool ok = tensor.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), &od_mtd, rmeta);
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
    ASSERT_TRUE(original.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), &od_mtd, rmeta));
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
    ASSERT_TRUE(original.convert_to_meta(reinterpret_cast<GstAnalyticsMtd *>(&group_mtd), &od_mtd, rmeta));
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
