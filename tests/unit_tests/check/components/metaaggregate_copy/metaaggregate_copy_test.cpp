/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file metaaggregate_copy_test.cpp
 * @brief Unit tests for copy_one_gst_analytics_mtd() and copy_all_gst_analytics_mtd()
 */

#include <glib.h>
#include <gst/analytics/analytics.h>
#include <gst/analytics/gstanalyticsclassificationmtd.h>
#include <gst/analytics/gstanalyticsgroupmtd.h>
#include <gst/analytics/gstanalyticskeypointmtd.h>
#include <gst/gst.h>
#include <gst/gstbuffer.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

extern "C" {
gboolean copy_one_gst_analytics_mtd(GstAnalyticsRelationMeta *dst, const GstAnalyticsMtd *mtd, GstAnalyticsMtd *new_mtd,
                                    gdouble scale_x, gdouble scale_y, GHashTable *id_map);
gboolean copy_all_gst_analytics_mtd(GstAnalyticsRelationMeta *src, GstAnalyticsRelationMeta *dst, GHashTable *id_map,
                                    gdouble scale_x, gdouble scale_y);
}

// ── Test fixture ─────────────────────────────────────────────────────────────

class MetaaggregateCopyTest : public ::testing::Test {
  protected:
    void SetUp() override {
        src_buf_ = gst_buffer_new();
        dst_buf_ = gst_buffer_new();
        src_meta_ = gst_buffer_add_analytics_relation_meta(src_buf_);
        dst_meta_ = gst_buffer_add_analytics_relation_meta(dst_buf_);
        id_map_ = g_hash_table_new(g_direct_hash, g_direct_equal);
    }

    void TearDown() override {
        g_hash_table_destroy(id_map_);
        gst_buffer_unref(src_buf_);
        gst_buffer_unref(dst_buf_);
    }

    GstBuffer *src_buf_ = nullptr;
    GstBuffer *dst_buf_ = nullptr;
    GstAnalyticsRelationMeta *src_meta_ = nullptr;
    GstAnalyticsRelationMeta *dst_meta_ = nullptr;
    GHashTable *id_map_ = nullptr;
};

// ══════════════════════════════════════════════════════════════════════════════
// copy_one_gst_analytics_mtd tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MetaaggregateCopyTest, CopyOD_NoScaling) {
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("car"), 100, 200,
                                                                300, 400, 0.0f, 0.95f, &od_mtd));

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &od_mtd, &new_mtd, 1.0, 1.0, id_map_));

    // Verify copied OD
    gint x, y, w, h;
    gfloat r, conf;
    ASSERT_TRUE(gst_analytics_od_mtd_get_oriented_location(&new_mtd, &x, &y, &w, &h, &r, &conf));
    EXPECT_EQ(x, 100);
    EXPECT_EQ(y, 200);
    EXPECT_EQ(w, 300);
    EXPECT_EQ(h, 400);
    EXPECT_FLOAT_EQ(r, 0.0f);
    EXPECT_FLOAT_EQ(conf, 0.95f);
    EXPECT_EQ(gst_analytics_od_mtd_get_obj_type(&new_mtd), g_quark_from_static_string("car"));
}

TEST_F(MetaaggregateCopyTest, CopyOD_WithScaling) {
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("person"), 100,
                                                                200, 50, 80, 0.0f, 0.8f, &od_mtd));

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &od_mtd, &new_mtd, 2.0, 0.5, id_map_));

    gint x, y, w, h;
    gfloat r, conf;
    ASSERT_TRUE(gst_analytics_od_mtd_get_oriented_location(&new_mtd, &x, &y, &w, &h, &r, &conf));
    // x and w scaled by 2.0: 100*2+0.5=200, 50*2+0.5=100
    EXPECT_EQ(x, 200);
    EXPECT_EQ(w, 100);
    // y and h scaled by 0.5: 200*0.5+0.5=100, 80*0.5+0.5=40
    EXPECT_EQ(y, 100);
    EXPECT_EQ(h, 40);
    EXPECT_FLOAT_EQ(conf, 0.8f);
}

TEST_F(MetaaggregateCopyTest, CopyOD_WithSemanticTag) {
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("face"), 10, 20,
                                                                30, 40, 0.0f, 0.99f, &od_mtd));
    gst_analytics_mtd_set_semantic_tag(&od_mtd, "face-detection-model");

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &od_mtd, &new_mtd, 1.0, 1.0, id_map_));

    gchar *tag = gst_analytics_mtd_get_semantic_tag(&new_mtd);
    ASSERT_NE(tag, nullptr);
    EXPECT_STREQ(tag, "face-detection-model");
    g_free(tag);
}

TEST_F(MetaaggregateCopyTest, CopyOD_EmptySemanticTag) {
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("dog"), 0, 0, 10,
                                                                10, 0.0f, 0.5f, &od_mtd));
    // Don't set semantic tag — should still copy fine

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &od_mtd, &new_mtd, 1.0, 1.0, id_map_));

    gchar *tag = gst_analytics_mtd_get_semantic_tag(&new_mtd);
    // Either NULL or empty string
    gboolean is_empty = (tag == nullptr || tag[0] == '\0');
    EXPECT_TRUE(is_empty);
    g_free(tag);
}

TEST_F(MetaaggregateCopyTest, CopyOD_WithRotation) {
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("plate"), 50, 60,
                                                                100, 30, 45.0f, 0.7f, &od_mtd));

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &od_mtd, &new_mtd, 1.0, 1.0, id_map_));

    gint x, y, w, h;
    gfloat r, conf;
    ASSERT_TRUE(gst_analytics_od_mtd_get_oriented_location(&new_mtd, &x, &y, &w, &h, &r, &conf));
    EXPECT_FLOAT_EQ(r, 45.0f);
}

TEST_F(MetaaggregateCopyTest, CopyClassification) {
    gfloat confidences[] = {0.1f, 0.3f, 0.6f};
    GQuark quarks[] = {g_quark_from_static_string("cat"), g_quark_from_static_string("dog"),
                       g_quark_from_static_string("bird")};

    GstAnalyticsMtd cls_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 3, confidences, quarks, &cls_mtd));

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &cls_mtd, &new_mtd, 1.0, 1.0, id_map_));

    EXPECT_EQ(gst_analytics_cls_mtd_get_length(&new_mtd), 3u);
    EXPECT_FLOAT_EQ(gst_analytics_cls_mtd_get_level(&new_mtd, 0), 0.1f);
    EXPECT_FLOAT_EQ(gst_analytics_cls_mtd_get_level(&new_mtd, 1), 0.3f);
    EXPECT_FLOAT_EQ(gst_analytics_cls_mtd_get_level(&new_mtd, 2), 0.6f);
    EXPECT_EQ(gst_analytics_cls_mtd_get_quark(&new_mtd, 0), g_quark_from_static_string("cat"));
    EXPECT_EQ(gst_analytics_cls_mtd_get_quark(&new_mtd, 1), g_quark_from_static_string("dog"));
    EXPECT_EQ(gst_analytics_cls_mtd_get_quark(&new_mtd, 2), g_quark_from_static_string("bird"));
}

TEST_F(MetaaggregateCopyTest, CopyClassification_WithSemanticTag) {
    gfloat confidences[] = {0.9f, 0.1f};
    GQuark quarks[] = {g_quark_from_static_string("happy"), g_quark_from_static_string("sad")};

    GstAnalyticsMtd cls_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 2, confidences, quarks, &cls_mtd));
    gst_analytics_mtd_set_semantic_tag(&cls_mtd, "emotion-recognition");

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &cls_mtd, &new_mtd, 1.0, 1.0, id_map_));

    gchar *tag = gst_analytics_mtd_get_semantic_tag(&new_mtd);
    ASSERT_NE(tag, nullptr);
    EXPECT_STREQ(tag, "emotion-recognition");
    g_free(tag);
}

TEST_F(MetaaggregateCopyTest, CopyClassification_SingleClass) {
    gfloat confidences[] = {1.0f};
    GQuark quarks[] = {g_quark_from_static_string("vehicle")};

    GstAnalyticsMtd cls_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, confidences, quarks, &cls_mtd));

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &cls_mtd, &new_mtd, 1.0, 1.0, id_map_));

    EXPECT_EQ(gst_analytics_cls_mtd_get_length(&new_mtd), 1u);
    EXPECT_FLOAT_EQ(gst_analytics_cls_mtd_get_level(&new_mtd, 0), 1.0f);
}

TEST_F(MetaaggregateCopyTest, CopyTracking) {
    GstAnalyticsMtd trk_mtd;
    GstClockTime first_seen = 1000000000; // 1 second
    ASSERT_TRUE(gst_analytics_relation_meta_add_tracking_mtd(src_meta_, 42, first_seen, &trk_mtd));

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &trk_mtd, &new_mtd, 1.0, 1.0, id_map_));

    guint64 tracking_id;
    GstClockTime ts_first, ts_last;
    gboolean lost;
    ASSERT_TRUE(gst_analytics_tracking_mtd_get_info(&new_mtd, &tracking_id, &ts_first, &ts_last, &lost));
    EXPECT_EQ(tracking_id, 42u);
    EXPECT_EQ(ts_first, first_seen);
}

TEST_F(MetaaggregateCopyTest, CopyKeypoint) {
    GstAnalyticsKeypointMtd kp_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_keypoint_mtd(src_meta_, GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D, 150, 250,
                                                             0, GST_ANALYTICS_KEYPOINT_VISIBILITY_VISIBLE, 0.85f,
                                                             &kp_mtd));

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, (GstAnalyticsMtd *)&kp_mtd, &new_mtd, 1.0, 1.0, id_map_));

    gint x, y, z;
    GstAnalyticsKeypointDimensions dim;
    gfloat confidence;
    guint8 visibility;
    ASSERT_TRUE(gst_analytics_keypoint_mtd_get_position((GstAnalyticsKeypointMtd *)&new_mtd, &x, &y, &z, &dim));
    ASSERT_TRUE(gst_analytics_keypoint_mtd_get_confidence((GstAnalyticsKeypointMtd *)&new_mtd, &confidence));
    ASSERT_TRUE(gst_analytics_keypoint_mtd_get_visibility_flags((GstAnalyticsKeypointMtd *)&new_mtd, &visibility));

    EXPECT_EQ(x, 150);
    EXPECT_EQ(y, 250);
    EXPECT_EQ(z, 0);
    EXPECT_EQ(dim, GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D);
    EXPECT_FLOAT_EQ(confidence, 0.85f);
    EXPECT_EQ(visibility, GST_ANALYTICS_KEYPOINT_VISIBILITY_VISIBLE);
}

TEST_F(MetaaggregateCopyTest, CopyKeypoint_WithSemanticTag) {
    GstAnalyticsKeypointMtd kp_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_keypoint_mtd(src_meta_, GST_ANALYTICS_KEYPOINT_DIMENSIONS_3D, 10, 20,
                                                             30, GST_ANALYTICS_KEYPOINT_VISIBILITY_VISIBLE, 0.9f,
                                                             &kp_mtd));
    gst_analytics_mtd_set_semantic_tag((GstAnalyticsMtd *)&kp_mtd, "pose-model/coco_18");

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, (GstAnalyticsMtd *)&kp_mtd, &new_mtd, 1.0, 1.0, id_map_));

    gchar *tag = gst_analytics_mtd_get_semantic_tag(&new_mtd);
    ASSERT_NE(tag, nullptr);
    EXPECT_STREQ(tag, "pose-model/coco_18");
    g_free(tag);
}

TEST_F(MetaaggregateCopyTest, CopyGroup_WithMembers) {
    // Create OD + classification in source, then group them
    GstAnalyticsMtd od_mtd, cls_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("person"), 10, 20,
                                                                100, 200, 0.0f, 0.9f, &od_mtd));
    gfloat confidences[] = {0.8f, 0.2f};
    GQuark quarks[] = {g_quark_from_static_string("male"), g_quark_from_static_string("female")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 2, confidences, quarks, &cls_mtd));

    GstAnalyticsGroupMtd grp_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 2, &grp_mtd));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp_mtd, od_mtd.id));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp_mtd, cls_mtd.id));
    gst_analytics_mtd_set_semantic_tag((GstAnalyticsMtd *)&grp_mtd, "person-model");

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, (GstAnalyticsMtd *)&grp_mtd, &new_mtd, 1.0, 1.0, id_map_));

    // Verify group semantic tag was copied
    gchar *tag = gst_analytics_mtd_get_semantic_tag(&new_mtd);
    ASSERT_NE(tag, nullptr);
    EXPECT_STREQ(tag, "person-model");
    g_free(tag);

    // Verify members were copied (group should have 2 members)
    gsize count = gst_analytics_group_mtd_get_member_count((GstAnalyticsGroupMtd *)&new_mtd);
    EXPECT_EQ(count, 2u);
}

TEST_F(MetaaggregateCopyTest, CopyGroup_MembersSemanticTagsPreserved) {
    // Create OD with tag, cls with tag, group them
    GstAnalyticsMtd od_mtd, cls_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("face"), 5, 5, 50,
                                                                50, 0.0f, 0.95f, &od_mtd));
    gst_analytics_mtd_set_semantic_tag(&od_mtd, "detector/retinaface");

    gfloat confidences[] = {0.7f};
    GQuark quarks[] = {g_quark_from_static_string("young")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, confidences, quarks, &cls_mtd));
    gst_analytics_mtd_set_semantic_tag(&cls_mtd, "age-model");

    GstAnalyticsGroupMtd grp_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 2, &grp_mtd));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp_mtd, od_mtd.id));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp_mtd, cls_mtd.id));

    GstAnalyticsMtd new_grp;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, (GstAnalyticsMtd *)&grp_mtd, &new_grp, 1.0, 1.0, id_map_));

    // Check that the members in dst have their semantic tags
    gpointer state = NULL;
    GstAnalyticsMtd member;
    int od_found = 0, cls_found = 0;
    while (gst_analytics_group_mtd_iterate((GstAnalyticsGroupMtd *)&new_grp, &state, GST_ANALYTICS_MTD_TYPE_ANY,
                                           &member)) {
        gchar *mtag = gst_analytics_mtd_get_semantic_tag(&member);
        GstAnalyticsMtdType t = gst_analytics_mtd_get_mtd_type(&member);
        if (t == gst_analytics_od_mtd_get_mtd_type()) {
            EXPECT_STREQ(mtag, "detector/retinaface");
            od_found++;
        } else if (t == gst_analytics_cls_mtd_get_mtd_type()) {
            EXPECT_STREQ(mtag, "age-model");
            cls_found++;
        }
        g_free(mtag);
    }
    EXPECT_EQ(od_found, 1);
    EXPECT_EQ(cls_found, 1);
}

TEST_F(MetaaggregateCopyTest, CopyOD_ZeroDimensions) {
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("point"), 0, 0, 0,
                                                                0, 0.0f, 1.0f, &od_mtd));

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &od_mtd, &new_mtd, 3.0, 3.0, id_map_));

    gint x, y, w, h;
    gfloat r, conf;
    ASSERT_TRUE(gst_analytics_od_mtd_get_oriented_location(&new_mtd, &x, &y, &w, &h, &r, &conf));
    // 0 * scale + 0.5 = 0 (cast to int)
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
    EXPECT_EQ(w, 0);
    EXPECT_EQ(h, 0);
}

TEST_F(MetaaggregateCopyTest, CopyOne_NullDst) {
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("x"), 1, 1, 1, 1,
                                                                0.0f, 0.5f, &od_mtd));

    GstAnalyticsMtd new_mtd;
    EXPECT_FALSE(copy_one_gst_analytics_mtd(nullptr, &od_mtd, &new_mtd, 1.0, 1.0, id_map_));
}

TEST_F(MetaaggregateCopyTest, CopyOne_NullIdMap) {
    // id_map can be NULL — function should still work for non-group types
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("obj"), 5, 5, 10,
                                                                10, 0.0f, 0.6f, &od_mtd));

    GstAnalyticsMtd new_mtd;
    EXPECT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &od_mtd, &new_mtd, 1.0, 1.0, nullptr));
}

// ══════════════════════════════════════════════════════════════════════════════
// copy_all_gst_analytics_mtd tests
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MetaaggregateCopyTest, CopyAll_EmptySource) {
    // Source has no analytics mtds — should succeed with nothing to copy
    EXPECT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));
    EXPECT_EQ(g_hash_table_size(id_map_), 0u);
}

TEST_F(MetaaggregateCopyTest, CopyAll_SingleOD) {
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("car"), 10, 20,
                                                                30, 40, 0.0f, 0.85f, &od_mtd));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));
    EXPECT_EQ(g_hash_table_size(id_map_), 1u);

    // Verify the OD exists in dst
    gpointer state = NULL;
    GstAnalyticsMtd dst_od;
    ASSERT_TRUE(gst_analytics_relation_meta_iterate(dst_meta_, &state, gst_analytics_od_mtd_get_mtd_type(), &dst_od));

    gint x, y, w, h;
    gfloat r, conf;
    ASSERT_TRUE(gst_analytics_od_mtd_get_oriented_location(&dst_od, &x, &y, &w, &h, &r, &conf));
    EXPECT_EQ(x, 10);
    EXPECT_EQ(y, 20);
}

TEST_F(MetaaggregateCopyTest, CopyAll_ODWithRelatedCls) {
    GstAnalyticsMtd od_mtd, cls_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("person"), 0, 0,
                                                                100, 200, 0.0f, 0.9f, &od_mtd));
    gfloat confs[] = {0.6f, 0.4f};
    GQuark qs[] = {g_quark_from_static_string("adult"), g_quark_from_static_string("child")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 2, confs, qs, &cls_mtd));

    // Set relation: cls is related to OD
    ASSERT_TRUE(
        gst_analytics_relation_meta_set_relation(src_meta_, GST_ANALYTICS_REL_TYPE_IS_PART_OF, cls_mtd.id, od_mtd.id));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));
    EXPECT_EQ(g_hash_table_size(id_map_), 2u);

    // Verify relation is preserved in dst
    gpointer val_od, val_cls;
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(od_mtd.id), NULL, &val_od));
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(cls_mtd.id), NULL, &val_cls));
    guint new_od_id = GPOINTER_TO_UINT(val_od);
    guint new_cls_id = GPOINTER_TO_UINT(val_cls);

    GstAnalyticsRelTypes rel = gst_analytics_relation_meta_get_relation(dst_meta_, new_cls_id, new_od_id);
    EXPECT_TRUE(rel & GST_ANALYTICS_REL_TYPE_IS_PART_OF);
}

TEST_F(MetaaggregateCopyTest, CopyAll_SkipsTracking) {
    GstAnalyticsMtd od_mtd, trk_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("person"), 10, 10,
                                                                50, 100, 0.0f, 0.9f, &od_mtd));
    ASSERT_TRUE(gst_analytics_relation_meta_add_tracking_mtd(src_meta_, 99, GST_CLOCK_TIME_NONE, &trk_mtd));
    ASSERT_TRUE(
        gst_analytics_relation_meta_set_relation(src_meta_, GST_ANALYTICS_REL_TYPE_IS_PART_OF, trk_mtd.id, od_mtd.id));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));

    // Only OD should be in id_map, tracking is skipped
    EXPECT_EQ(g_hash_table_size(id_map_), 1u);
    EXPECT_TRUE(g_hash_table_contains(id_map_, GUINT_TO_POINTER(od_mtd.id)));
    EXPECT_FALSE(g_hash_table_contains(id_map_, GUINT_TO_POINTER(trk_mtd.id)));
}

TEST_F(MetaaggregateCopyTest, CopyAll_GroupMembersNotCopiedIndividually) {
    // Members of a group should be copied as part of the group, not independently
    GstAnalyticsMtd od_mtd, cls_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("face"), 0, 0, 50,
                                                                50, 0.0f, 0.95f, &od_mtd));
    gfloat confs[] = {1.0f};
    GQuark qs[] = {g_quark_from_static_string("smile")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, confs, qs, &cls_mtd));

    GstAnalyticsGroupMtd grp;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 2, &grp));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp, od_mtd.id));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp, cls_mtd.id));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));

    // All three (group + 2 members) should be in id_map
    EXPECT_EQ(g_hash_table_size(id_map_), 3u);

    // Count ODs in dst — should be exactly 1 (not duplicated)
    gpointer state = NULL;
    GstAnalyticsMtd iter_mtd;
    int od_count = 0;
    while (gst_analytics_relation_meta_iterate(dst_meta_, &state, gst_analytics_od_mtd_get_mtd_type(), &iter_mtd)) {
        od_count++;
    }
    EXPECT_EQ(od_count, 1);
}

TEST_F(MetaaggregateCopyTest, CopyAll_WithScaling) {
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("obj"), 100, 200,
                                                                50, 80, 0.0f, 0.7f, &od_mtd));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 0.5, 2.0));

    gpointer state = NULL;
    GstAnalyticsMtd dst_od;
    ASSERT_TRUE(gst_analytics_relation_meta_iterate(dst_meta_, &state, gst_analytics_od_mtd_get_mtd_type(), &dst_od));

    gint x, y, w, h;
    gfloat r, conf;
    ASSERT_TRUE(gst_analytics_od_mtd_get_oriented_location(&dst_od, &x, &y, &w, &h, &r, &conf));
    // x: 100*0.5+0.5=50, w: 50*0.5+0.5=25
    EXPECT_EQ(x, 50);
    EXPECT_EQ(w, 25);
    // y: 200*2.0+0.5=400, h: 80*2.0+0.5=160
    EXPECT_EQ(y, 400);
    EXPECT_EQ(h, 160);
}

TEST_F(MetaaggregateCopyTest, CopyAll_SemanticTagsPreserved) {
    GstAnalyticsMtd od_mtd, cls_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("person"), 0, 0,
                                                                100, 100, 0.0f, 0.9f, &od_mtd));
    gst_analytics_mtd_set_semantic_tag(&od_mtd, "yolo-v8");

    gfloat confs[] = {0.5f, 0.5f};
    GQuark qs[] = {g_quark_from_static_string("a"), g_quark_from_static_string("b")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 2, confs, qs, &cls_mtd));
    gst_analytics_mtd_set_semantic_tag(&cls_mtd, "classifier-v2");

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));

    // Verify tags on dst
    gpointer v_od, v_cls;
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(od_mtd.id), NULL, &v_od));
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(cls_mtd.id), NULL, &v_cls));
    guint new_od_id = GPOINTER_TO_UINT(v_od);
    guint new_cls_id = GPOINTER_TO_UINT(v_cls);

    GstAnalyticsMtd dst_od, dst_cls;
    ASSERT_TRUE(
        gst_analytics_relation_meta_get_mtd(dst_meta_, new_od_id, gst_analytics_od_mtd_get_mtd_type(), &dst_od));
    ASSERT_TRUE(
        gst_analytics_relation_meta_get_mtd(dst_meta_, new_cls_id, gst_analytics_cls_mtd_get_mtd_type(), &dst_cls));

    gchar *tag_od = gst_analytics_mtd_get_semantic_tag(&dst_od);
    gchar *tag_cls = gst_analytics_mtd_get_semantic_tag(&dst_cls);
    EXPECT_STREQ(tag_od, "yolo-v8");
    EXPECT_STREQ(tag_cls, "classifier-v2");
    g_free(tag_od);
    g_free(tag_cls);
}

TEST_F(MetaaggregateCopyTest, CopyAll_MultipleODs) {
    for (int i = 0; i < 5; i++) {
        GstAnalyticsMtd od;
        ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(
            src_meta_, g_quark_from_static_string("obj"), i * 10, i * 20, 50, 50, 0.0f, 0.5f + i * 0.1f, &od));
    }

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));
    EXPECT_EQ(g_hash_table_size(id_map_), 5u);

    // Count ODs in dst
    gpointer state = NULL;
    GstAnalyticsMtd iter_mtd;
    int count = 0;
    while (gst_analytics_relation_meta_iterate(dst_meta_, &state, gst_analytics_od_mtd_get_mtd_type(), &iter_mtd)) {
        count++;
    }
    EXPECT_EQ(count, 5);
}

TEST_F(MetaaggregateCopyTest, CopyAll_NullSrc) {
    EXPECT_FALSE(copy_all_gst_analytics_mtd(nullptr, dst_meta_, id_map_, 1.0, 1.0));
}

TEST_F(MetaaggregateCopyTest, CopyAll_NullDst) {
    EXPECT_FALSE(copy_all_gst_analytics_mtd(src_meta_, nullptr, id_map_, 1.0, 1.0));
}

TEST_F(MetaaggregateCopyTest, CopyAll_NullIdMap) {
    EXPECT_FALSE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, nullptr, 1.0, 1.0));
}

TEST_F(MetaaggregateCopyTest, CopyAll_MultipleRelations) {
    // OD -> cls1, OD -> cls2
    GstAnalyticsMtd od_mtd, cls1, cls2;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("person"), 0, 0,
                                                                100, 200, 0.0f, 0.9f, &od_mtd));

    gfloat c1[] = {0.8f};
    GQuark q1[] = {g_quark_from_static_string("male")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, c1, q1, &cls1));

    gfloat c2[] = {0.7f};
    GQuark q2[] = {g_quark_from_static_string("adult")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, c2, q2, &cls2));

    ASSERT_TRUE(
        gst_analytics_relation_meta_set_relation(src_meta_, GST_ANALYTICS_REL_TYPE_IS_PART_OF, cls1.id, od_mtd.id));
    ASSERT_TRUE(
        gst_analytics_relation_meta_set_relation(src_meta_, GST_ANALYTICS_REL_TYPE_IS_PART_OF, cls2.id, od_mtd.id));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));

    gpointer v_od, v_cls1, v_cls2;
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(od_mtd.id), NULL, &v_od));
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(cls1.id), NULL, &v_cls1));
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(cls2.id), NULL, &v_cls2));
    guint new_od = GPOINTER_TO_UINT(v_od);
    guint new_cls1_id = GPOINTER_TO_UINT(v_cls1);
    guint new_cls2_id = GPOINTER_TO_UINT(v_cls2);

    GstAnalyticsRelTypes rel1 = gst_analytics_relation_meta_get_relation(dst_meta_, new_cls1_id, new_od);
    GstAnalyticsRelTypes rel2 = gst_analytics_relation_meta_get_relation(dst_meta_, new_cls2_id, new_od);
    EXPECT_TRUE(rel1 & GST_ANALYTICS_REL_TYPE_IS_PART_OF);
    EXPECT_TRUE(rel2 & GST_ANALYTICS_REL_TYPE_IS_PART_OF);
}

// ══════════════════════════════════════════════════════════════════════════════
// Additional edge cases — copy_one_gst_analytics_mtd
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MetaaggregateCopyTest, CopyGroup_Empty) {
    // Group with zero members
    GstAnalyticsGroupMtd grp;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 0, &grp));
    gst_analytics_mtd_set_semantic_tag((GstAnalyticsMtd *)&grp, "empty-group");

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, (GstAnalyticsMtd *)&grp, &new_mtd, 1.0, 1.0, id_map_));

    gsize count = gst_analytics_group_mtd_get_member_count((GstAnalyticsGroupMtd *)&new_mtd);
    EXPECT_EQ(count, 0u);

    gchar *tag = gst_analytics_mtd_get_semantic_tag(&new_mtd);
    EXPECT_STREQ(tag, "empty-group");
    g_free(tag);
}

TEST_F(MetaaggregateCopyTest, CopyGroup_Nested) {
    // Inner group with one OD member
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("hand"), 5, 5, 20,
                                                                20, 0.0f, 0.8f, &od_mtd));

    GstAnalyticsGroupMtd inner_grp;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 1, &inner_grp));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&inner_grp, od_mtd.id));
    gst_analytics_mtd_set_semantic_tag((GstAnalyticsMtd *)&inner_grp, "inner");

    // Outer group containing the inner group
    GstAnalyticsGroupMtd outer_grp;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 1, &outer_grp));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&outer_grp, inner_grp.id));
    gst_analytics_mtd_set_semantic_tag((GstAnalyticsMtd *)&outer_grp, "outer");

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, (GstAnalyticsMtd *)&outer_grp, &new_mtd, 1.0, 1.0, id_map_));

    // Outer group tag preserved
    gchar *tag = gst_analytics_mtd_get_semantic_tag(&new_mtd);
    EXPECT_STREQ(tag, "outer");
    g_free(tag);

    // Outer has 1 member (inner group)
    gsize outer_count = gst_analytics_group_mtd_get_member_count((GstAnalyticsGroupMtd *)&new_mtd);
    EXPECT_EQ(outer_count, 1u);

    // Find the inner group member and verify its tag
    gpointer state = NULL;
    GstAnalyticsMtd member;
    ASSERT_TRUE(
        gst_analytics_group_mtd_iterate((GstAnalyticsGroupMtd *)&new_mtd, &state, GST_ANALYTICS_MTD_TYPE_ANY, &member));
    gchar *inner_tag = gst_analytics_mtd_get_semantic_tag(&member);
    EXPECT_STREQ(inner_tag, "inner");
    g_free(inner_tag);
}

TEST_F(MetaaggregateCopyTest, CopyOD_NegativeCoordinates) {
    // Negative coordinates are valid in pixel space (partially offscreen)
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("obj"), -10, -20,
                                                                100, 200, 0.0f, 0.7f, &od_mtd));

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &od_mtd, &new_mtd, 1.0, 1.0, id_map_));

    gint x, y, w, h;
    gfloat r, conf;
    ASSERT_TRUE(gst_analytics_od_mtd_get_oriented_location(&new_mtd, &x, &y, &w, &h, &r, &conf));
    EXPECT_EQ(x, -10);
    EXPECT_EQ(y, -20);
}

TEST_F(MetaaggregateCopyTest, CopyOD_LargeScaling) {
    // Very large scaling factor — check no overflow for reasonable coords
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("obj"), 100, 200,
                                                                300, 400, 0.0f, 0.9f, &od_mtd));

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &od_mtd, &new_mtd, 10.0, 10.0, id_map_));

    gint x, y, w, h;
    gfloat r, conf;
    ASSERT_TRUE(gst_analytics_od_mtd_get_oriented_location(&new_mtd, &x, &y, &w, &h, &r, &conf));
    EXPECT_EQ(x, 1000);
    EXPECT_EQ(y, 2000);
    EXPECT_EQ(w, 3000);
    EXPECT_EQ(h, 4000);
}

TEST_F(MetaaggregateCopyTest, CopyOD_FractionalScaling) {
    // Very small scaling — verify rounding
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("obj"), 1000,
                                                                1000, 500, 500, 0.0f, 0.9f, &od_mtd));

    GstAnalyticsMtd new_mtd;
    ASSERT_TRUE(copy_one_gst_analytics_mtd(dst_meta_, &od_mtd, &new_mtd, 0.001, 0.001, id_map_));

    gint x, y, w, h;
    gfloat r, conf;
    ASSERT_TRUE(gst_analytics_od_mtd_get_oriented_location(&new_mtd, &x, &y, &w, &h, &r, &conf));
    // 1000 * 0.001 + 0.5 = 1.5 → (int)1
    EXPECT_EQ(x, 1);
    EXPECT_EQ(y, 1);
    // 500 * 0.001 + 0.5 = 1.0 → (int)1
    EXPECT_EQ(w, 1);
    EXPECT_EQ(h, 1);
}

// ══════════════════════════════════════════════════════════════════════════════
// Additional edge cases — copy_all_gst_analytics_mtd
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(MetaaggregateCopyTest, CopyAll_RelateToRelationType) {
    // Test with GST_ANALYTICS_REL_TYPE_RELATE_TO (used for cls→od in production)
    GstAnalyticsMtd od_mtd, cls_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("person"), 0, 0,
                                                                100, 200, 0.0f, 0.9f, &od_mtd));
    gfloat confs[] = {0.9f};
    GQuark qs[] = {g_quark_from_static_string("happy")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, confs, qs, &cls_mtd));

    // OD CONTAIN cls (standard production pattern)
    ASSERT_TRUE(
        gst_analytics_relation_meta_set_relation(src_meta_, GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd.id, cls_mtd.id));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));

    gpointer v_od, v_cls;
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(od_mtd.id), NULL, &v_od));
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(cls_mtd.id), NULL, &v_cls));

    GstAnalyticsRelTypes rel =
        gst_analytics_relation_meta_get_relation(dst_meta_, GPOINTER_TO_UINT(v_od), GPOINTER_TO_UINT(v_cls));
    EXPECT_TRUE(rel & GST_ANALYTICS_REL_TYPE_CONTAIN);
}

TEST_F(MetaaggregateCopyTest, CopyAll_RelateToRelation) {
    // GST_ANALYTICS_REL_TYPE_RELATE_TO — generic relation used for keypoints
    GstAnalyticsMtd od_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("person"), 10, 10,
                                                                80, 160, 0.0f, 0.9f, &od_mtd));

    GstAnalyticsKeypointMtd kp_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_keypoint_mtd(src_meta_, GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D, 50, 80, 0,
                                                             GST_ANALYTICS_KEYPOINT_VISIBILITY_VISIBLE, 0.95f,
                                                             &kp_mtd));

    ASSERT_TRUE(
        gst_analytics_relation_meta_set_relation(src_meta_, GST_ANALYTICS_REL_TYPE_RELATE_TO, od_mtd.id, kp_mtd.id));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));

    gpointer v_od, v_kp;
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(od_mtd.id), NULL, &v_od));
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(kp_mtd.id), NULL, &v_kp));

    GstAnalyticsRelTypes rel =
        gst_analytics_relation_meta_get_relation(dst_meta_, GPOINTER_TO_UINT(v_od), GPOINTER_TO_UINT(v_kp));
    EXPECT_TRUE(rel & GST_ANALYTICS_REL_TYPE_RELATE_TO);
}

TEST_F(MetaaggregateCopyTest, CopyAll_TrackingRelationSkipped) {
    // Relation to a tracking mtd should be gracefully skipped (not crash)
    GstAnalyticsMtd od_mtd, cls_mtd, trk_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("car"), 0, 0, 50,
                                                                50, 0.0f, 0.8f, &od_mtd));
    gfloat confs[] = {0.7f};
    GQuark qs[] = {g_quark_from_static_string("sedan")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, confs, qs, &cls_mtd));
    ASSERT_TRUE(gst_analytics_relation_meta_add_tracking_mtd(src_meta_, 7, 0, &trk_mtd));

    // OD → tracking, OD → cls
    ASSERT_TRUE(
        gst_analytics_relation_meta_set_relation(src_meta_, GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd.id, trk_mtd.id));
    ASSERT_TRUE(
        gst_analytics_relation_meta_set_relation(src_meta_, GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd.id, cls_mtd.id));

    // Should succeed — tracking is skipped, cls relation is copied
    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));
    EXPECT_EQ(g_hash_table_size(id_map_), 2u); // OD + cls, no tracking

    gpointer v_od, v_cls;
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(od_mtd.id), NULL, &v_od));
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(cls_mtd.id), NULL, &v_cls));

    GstAnalyticsRelTypes rel =
        gst_analytics_relation_meta_get_relation(dst_meta_, GPOINTER_TO_UINT(v_od), GPOINTER_TO_UINT(v_cls));
    EXPECT_TRUE(rel & GST_ANALYTICS_REL_TYPE_CONTAIN);
}

TEST_F(MetaaggregateCopyTest, CopyAll_MultipleGroupsAndStandalone) {
    // Two groups + one standalone OD not in any group
    GstAnalyticsMtd od1, od2, od3, cls1, cls2;

    // Group 1: od1 + cls1
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("person"), 0, 0,
                                                                100, 200, 0.0f, 0.9f, &od1));
    gfloat c1[] = {0.8f};
    GQuark q1[] = {g_quark_from_static_string("male")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, c1, q1, &cls1));

    GstAnalyticsGroupMtd grp1;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 2, &grp1));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp1, od1.id));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp1, cls1.id));

    // Group 2: od2 + cls2
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("car"), 200, 100,
                                                                80, 60, 0.0f, 0.85f, &od2));
    gfloat c2[] = {0.6f};
    GQuark q2[] = {g_quark_from_static_string("SUV")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, c2, q2, &cls2));

    GstAnalyticsGroupMtd grp2;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 2, &grp2));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp2, od2.id));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp2, cls2.id));

    // Standalone OD (not in any group)
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("bicycle"), 300,
                                                                300, 40, 40, 0.0f, 0.75f, &od3));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));

    // 2 groups + 2*2 members + 1 standalone = 7
    EXPECT_EQ(g_hash_table_size(id_map_), 7u);

    // Count ODs in dst — should be exactly 3
    gpointer state = NULL;
    GstAnalyticsMtd iter_mtd;
    int od_count = 0;
    while (gst_analytics_relation_meta_iterate(dst_meta_, &state, gst_analytics_od_mtd_get_mtd_type(), &iter_mtd)) {
        od_count++;
    }
    EXPECT_EQ(od_count, 3);

    // Count groups in dst — should be 2
    state = NULL;
    int grp_count = 0;
    while (gst_analytics_relation_meta_iterate(dst_meta_, &state, gst_analytics_group_mtd_get_mtd_type(), &iter_mtd)) {
        grp_count++;
    }
    EXPECT_EQ(grp_count, 2);
}

TEST_F(MetaaggregateCopyTest, CopyAll_RelationWithIdZero_BugRegression) {
    // Regression test: when dst mtd gets id=0, relation copy must still work.
    // The first mtd added to an empty buffer always gets id=0.
    GstAnalyticsMtd od_mtd, cls_mtd;
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("obj"), 10, 10,
                                                                50, 50, 0.0f, 0.9f, &od_mtd));
    gfloat confs[] = {0.5f};
    GQuark qs[] = {g_quark_from_static_string("label")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, confs, qs, &cls_mtd));
    ASSERT_TRUE(
        gst_analytics_relation_meta_set_relation(src_meta_, GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd.id, cls_mtd.id));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));

    // Verify the first mtd in dst indeed got id=0
    gpointer v_first;
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(od_mtd.id), NULL, &v_first));
    guint first_dst_id = GPOINTER_TO_UINT(v_first);
    EXPECT_EQ(first_dst_id, 0u); // confirms the id=0 scenario

    // Relation must still be preserved
    gpointer v_cls;
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(cls_mtd.id), NULL, &v_cls));
    GstAnalyticsRelTypes rel =
        gst_analytics_relation_meta_get_relation(dst_meta_, first_dst_id, GPOINTER_TO_UINT(v_cls));
    EXPECT_TRUE(rel & GST_ANALYTICS_REL_TYPE_CONTAIN);
}

TEST_F(MetaaggregateCopyTest, CopyAll_GroupMembersRelationsToOutsideMtd) {
    // Group member has a relation to an mtd outside the group
    GstAnalyticsMtd od_mtd, cls_in_group, cls_outside;

    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("person"), 0, 0,
                                                                100, 200, 0.0f, 0.9f, &od_mtd));
    gfloat c1[] = {0.8f};
    GQuark q1[] = {g_quark_from_static_string("male")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, c1, q1, &cls_in_group));

    // Group: od + cls_in_group
    GstAnalyticsGroupMtd grp;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 2, &grp));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp, od_mtd.id));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp, cls_in_group.id));

    // Standalone cls outside group
    gfloat c2[] = {0.6f};
    GQuark q2[] = {g_quark_from_static_string("age30")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, c2, q2, &cls_outside));

    // Relation: od (in group) CONTAIN cls_outside (standalone)
    ASSERT_TRUE(
        gst_analytics_relation_meta_set_relation(src_meta_, GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd.id, cls_outside.id));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));

    // group(2 members) + group itself + cls_outside = 4 entries in id_map
    EXPECT_EQ(g_hash_table_size(id_map_), 4u);

    // Verify the cross-group relation is preserved
    gpointer v_od, v_cls_out;
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(od_mtd.id), NULL, &v_od));
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(cls_outside.id), NULL, &v_cls_out));

    GstAnalyticsRelTypes rel =
        gst_analytics_relation_meta_get_relation(dst_meta_, GPOINTER_TO_UINT(v_od), GPOINTER_TO_UINT(v_cls_out));
    EXPECT_TRUE(rel & GST_ANALYTICS_REL_TYPE_CONTAIN);
}

TEST_F(MetaaggregateCopyTest, CopyAll_RelationBetweenMembersOfDifferentGroups) {
    // OD in group A has CONTAIN relation to cls in group B.
    // Both are group members (skipped at top-level), copied by their respective groups.
    // The relation copy loop at the end should still find both in id_map and preserve the relation.
    GstAnalyticsMtd od_a, cls_a, od_b, cls_b;

    // Group A members
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("person"), 0, 0,
                                                                100, 200, 0.0f, 0.9f, &od_a));
    gfloat ca[] = {0.8f};
    GQuark qa[] = {g_quark_from_static_string("male")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, ca, qa, &cls_a));

    GstAnalyticsGroupMtd grp_a;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 2, &grp_a));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp_a, od_a.id));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp_a, cls_a.id));

    // Group B members
    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("car"), 200, 100,
                                                                80, 60, 0.0f, 0.85f, &od_b));
    gfloat cb[] = {0.6f};
    GQuark qb[] = {g_quark_from_static_string("red")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, cb, qb, &cls_b));

    GstAnalyticsGroupMtd grp_b;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 2, &grp_b));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp_b, od_b.id));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp_b, cls_b.id));

    // Cross-group relation: od_a CONTAIN cls_b
    ASSERT_TRUE(gst_analytics_relation_meta_set_relation(src_meta_, GST_ANALYTICS_REL_TYPE_CONTAIN, od_a.id, cls_b.id));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));

    // Verify cross-group relation is preserved
    gpointer v_od_a, v_cls_b;
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(od_a.id), NULL, &v_od_a));
    ASSERT_TRUE(g_hash_table_lookup_extended(id_map_, GUINT_TO_POINTER(cls_b.id), NULL, &v_cls_b));

    GstAnalyticsRelTypes rel =
        gst_analytics_relation_meta_get_relation(dst_meta_, GPOINTER_TO_UINT(v_od_a), GPOINTER_TO_UINT(v_cls_b));
    EXPECT_TRUE(rel & GST_ANALYTICS_REL_TYPE_CONTAIN);
}

TEST_F(MetaaggregateCopyTest, CopyAll_SharedMemberBetweenTwoGroups) {
    // A single OD mtd is a member of both group A and group B.
    // Current behavior: the shared mtd is copied TWICE (once per group),
    // resulting in two separate mtds in the destination.
    // The id_map stores only the last mapping (overwritten by second group).
    GstAnalyticsMtd shared_od, cls_a, cls_b;

    ASSERT_TRUE(gst_analytics_relation_meta_add_oriented_od_mtd(src_meta_, g_quark_from_static_string("person"), 10, 20,
                                                                100, 200, 0.0f, 0.95f, &shared_od));
    gst_analytics_mtd_set_semantic_tag(&shared_od, "detector-model");

    // cls for group A
    gfloat ca[] = {0.8f};
    GQuark qa[] = {g_quark_from_static_string("male")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, ca, qa, &cls_a));

    // cls for group B
    gfloat cb[] = {0.7f};
    GQuark qb[] = {g_quark_from_static_string("adult")};
    ASSERT_TRUE(gst_analytics_relation_meta_add_cls_mtd(src_meta_, 1, cb, qb, &cls_b));

    // Group A: shared_od + cls_a
    GstAnalyticsGroupMtd grp_a;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 2, &grp_a));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp_a, shared_od.id));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp_a, cls_a.id));

    // Group B: shared_od + cls_b
    GstAnalyticsGroupMtd grp_b;
    ASSERT_TRUE(gst_analytics_relation_meta_add_group_mtd_with_size(src_meta_, 2, &grp_b));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp_b, shared_od.id));
    ASSERT_TRUE(gst_analytics_group_mtd_add_member(&grp_b, cls_b.id));

    ASSERT_TRUE(copy_all_gst_analytics_mtd(src_meta_, dst_meta_, id_map_, 1.0, 1.0));

    // Count ODs in destination — shared_od is reused (copied once, referenced by both groups)
    gpointer state = NULL;
    GstAnalyticsMtd iter_mtd;
    int od_count = 0;
    while (gst_analytics_relation_meta_iterate(dst_meta_, &state, gst_analytics_od_mtd_get_mtd_type(), &iter_mtd)) {
        od_count++;
    }
    EXPECT_EQ(od_count, 1);

    // Both copies should have the semantic tag preserved
    state = NULL;
    while (gst_analytics_relation_meta_iterate(dst_meta_, &state, gst_analytics_od_mtd_get_mtd_type(), &iter_mtd)) {
        gchar *tag = gst_analytics_mtd_get_semantic_tag(&iter_mtd);
        EXPECT_STREQ(tag, "detector-model");
        g_free(tag);
    }

    // Each group in dst should have 2 members
    state = NULL;
    int grp_count = 0;
    while (gst_analytics_relation_meta_iterate(dst_meta_, &state, gst_analytics_group_mtd_get_mtd_type(), &iter_mtd)) {
        gsize count = gst_analytics_group_mtd_get_member_count((GstAnalyticsGroupMtd *)&iter_mtd);
        EXPECT_EQ(count, 2u);
        grp_count++;
    }
    EXPECT_EQ(grp_count, 2);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    gst_init(&argc, &argv);
    return RUN_ALL_TESTS();
}
