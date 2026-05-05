/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file keypoint_descriptor_test.cpp
 * @brief Unit tests for GstAnalyticsKeypointDescriptor lookup and field access.
 *        Mirrors tests in tests/unit_tests/tests_gstgva/test_keypoint_descriptor.py
 */

#include "dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h"

#include <cstring>

#include <gtest/gtest.h>

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(KeypointDescriptorLookup, Coco17) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    ASSERT_NE(desc, nullptr);
}

TEST(KeypointDescriptorLookup, OpenPose18) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_OPENPOSE_18);
    ASSERT_NE(desc, nullptr);
}

TEST(KeypointDescriptorLookup, HrnetCoco17) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_HRNET_COCO_17);
    ASSERT_NE(desc, nullptr);
}

TEST(KeypointDescriptorLookup, CenterFace5) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5);
    ASSERT_NE(desc, nullptr);
}

TEST(KeypointDescriptorLookup, UnknownReturnsNull) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup("nonexistent-tag");
    EXPECT_EQ(desc, nullptr);
}

TEST(KeypointDescriptorLookup, NullReturnsNull) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(nullptr);
    EXPECT_EQ(desc, nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Semantic tag tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(KeypointDescriptorSemanticTag, Coco17) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc->semantic_tag, "body-pose/coco-17");
}

TEST(KeypointDescriptorSemanticTag, OpenPose18) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_OPENPOSE_18);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc->semantic_tag, "body-pose/openpose-18");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Point count tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(KeypointDescriptorPointCount, Coco17) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->point_count, 17u);
}

TEST(KeypointDescriptorPointCount, OpenPose18) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_OPENPOSE_18);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->point_count, 18u);
}

TEST(KeypointDescriptorPointCount, CenterFace5) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->point_count, 5u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Point name tests (direct field access)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(KeypointDescriptorPointName, Coco17First) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc->point_names[0], "nose");
}

TEST(KeypointDescriptorPointName, Coco17Last) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc->point_names[16], "ankle_r");
}

TEST(KeypointDescriptorPointName, OpenPose18Neck) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_OPENPOSE_18);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc->point_names[1], "neck");
}

TEST(KeypointDescriptorPointName, CenterFace5NoseTip) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc->point_names[2], "nose_tip");
}

TEST(KeypointDescriptorPointName, AllCoco17Names) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    ASSERT_NE(desc, nullptr);

    const char *expected[] = {"nose",       "eye_l",   "eye_r",   "ear_l",   "ear_r",   "shoulder_l",
                              "shoulder_r", "elbow_l", "elbow_r", "wrist_l", "wrist_r", "hip_l",
                              "hip_r",      "knee_l",  "knee_r",  "ankle_l", "ankle_r"};

    ASSERT_EQ(desc->point_count, 17u);
    for (gsize i = 0; i < desc->point_count; i++) {
        EXPECT_STREQ(desc->point_names[i], expected[i]) << "keypoint " << i;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Skeleton connection count tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(KeypointDescriptorSkeletonCount, Coco17) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->skeleton_connection_count, 18u);
}

TEST(KeypointDescriptorSkeletonCount, OpenPose18) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_OPENPOSE_18);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->skeleton_connection_count, 13u);
}

TEST(KeypointDescriptorSkeletonCount, CenterFace5) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->skeleton_connection_count, 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Skeleton connection tests (direct field access)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(KeypointDescriptorSkeleton, Coco17FirstConnection) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    ASSERT_NE(desc, nullptr);
    ASSERT_NE(desc->skeleton_connections, nullptr);
    EXPECT_EQ(desc->skeleton_connections[0], 0); // nose
    EXPECT_EQ(desc->skeleton_connections[1], 1); // eye_l
}

TEST(KeypointDescriptorSkeleton, Coco17LastConnection) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    ASSERT_NE(desc, nullptr);
    gsize last = desc->skeleton_connection_count - 1;
    EXPECT_EQ(desc->skeleton_connections[last * 2], 14);     // knee_r
    EXPECT_EQ(desc->skeleton_connections[last * 2 + 1], 16); // ankle_r
}

TEST(KeypointDescriptorSkeleton, CenterFace5NoSkeleton) {
    const auto *desc = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5);
    ASSERT_NE(desc, nullptr);
    EXPECT_EQ(desc->skeleton_connections, nullptr);
    EXPECT_EQ(desc->skeleton_connection_count, 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cross-descriptor comparison tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST(KeypointDescriptorComparison, HrnetSharesPointNamesWithCoco17) {
    const auto *coco = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    const auto *hrnet = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_HRNET_COCO_17);
    ASSERT_NE(coco, nullptr);
    ASSERT_NE(hrnet, nullptr);

    ASSERT_EQ(coco->point_count, hrnet->point_count);
    for (gsize i = 0; i < coco->point_count; i++) {
        EXPECT_STREQ(coco->point_names[i], hrnet->point_names[i]) << "keypoint " << i;
    }
}

TEST(KeypointDescriptorComparison, HrnetDifferentSkeletonFromCoco17) {
    const auto *coco = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    const auto *hrnet = gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_HRNET_COCO_17);
    ASSERT_NE(coco, nullptr);
    ASSERT_NE(hrnet, nullptr);

    // Both have 17 points but different skeleton connection counts
    EXPECT_NE(coco->skeleton_connection_count, hrnet->skeleton_connection_count);
}
