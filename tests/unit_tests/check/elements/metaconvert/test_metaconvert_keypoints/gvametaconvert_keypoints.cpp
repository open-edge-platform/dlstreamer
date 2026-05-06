/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gstgvametaconvert.h>

#include <dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h>
#include <dlstreamer/gst/metadata/gstanalyticskeypointmtd.h>
#include <dlstreamer/gst/metadata/gva_tensor_meta.h>

#include "test_common.h"

#include "glib.h"
#include "gst/analytics/analytics.h"
#include "gst/check/internal-check.h"
#include "gva_json_meta.h"
#include "region_of_interest.h"
#include "test_utils.h"

#include <gst/video/video.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <vector>

using json = nlohmann::json;

/* ── helpers ────────────────────────────────────────────────────────────── */

constexpr float kFloatTolerance = 1e-6f;

static void assert_json_float(const json &jvalue, double expected, const char *message) {
    ck_assert_msg(jvalue.is_number(), "%s: expected numeric value", message);
    ck_assert_msg(std::abs(jvalue.get<double>() - expected) < kFloatTolerance, "%s: expected %.6f, got %.6f", message,
                  expected, jvalue.get<double>());
}

/* ── test data structures ───────────────────────────────────────────────── */

struct KeypointTestData {
    Resolution resolution;
    /* OD bbox (pixel coords) */
    gint od_x, od_y, od_w, od_h;
    gdouble od_confidence;
    /* Whether to add keypoints at all */
    bool add_keypoints;
    /* Keypoints — group 1 */
    const char *semantic_tag;
    GstAnalyticsKeypointDimensions dimension;
    std::vector<gint> positions; /* flat [x,y,...] or [x,y,z,...] */
    gsize keypoint_count;
    std::vector<gfloat> confidences;
    std::vector<gint> skeleton_pairs; /* flat [from,to,...] */
    /* Optional second group */
    bool add_second_group;
    const char *semantic_tag_2;
    std::vector<gint> positions_2;
    gsize keypoint_count_2;
    std::vector<gfloat> confidences_2;
    std::vector<gint> skeleton_pairs_2;
};

/* ── pad templates ──────────────────────────────────────────────────────── */

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

/* ── setup / check callbacks ────────────────────────────────────────────── */

static void setup_keypoint_inbuffer(GstBuffer *inbuffer, gpointer user_data) {
    KeypointTestData *td = static_cast<KeypointTestData *>(user_data);
    ck_assert_msg(td != NULL, "Passed data is not KeypointTestData");

    GstVideoInfo info;
    gst_video_info_set_format(&info, TEST_BUFFER_VIDEO_FORMAT, td->resolution.width, td->resolution.height);
    gst_buffer_add_video_meta(inbuffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_INFO_FORMAT(&info),
                              GST_VIDEO_INFO_WIDTH(&info), GST_VIDEO_INFO_HEIGHT(&info));

    /* Add analytics relation meta with OD */
    GstAnalyticsRelationMeta *rmeta = gst_buffer_add_analytics_relation_meta(inbuffer);
    ck_assert_msg(rmeta != NULL, "Failed to add analytics relation meta");

    GstAnalyticsODMtd od_mtd;
    gboolean ret = gst_analytics_relation_meta_add_oriented_od_mtd(rmeta, 0, td->od_x, td->od_y, td->od_w, td->od_h,
                                                                   0.0, td->od_confidence, &od_mtd);
    ck_assert_msg(ret == TRUE, "Failed to add OD mtd");

    /* Legacy ROI (needed so gvametaconvert iterates over regions) */
    GstVideoRegionOfInterestMeta *roi_meta =
        gst_buffer_add_video_region_of_interest_meta(inbuffer, "person", td->od_x, td->od_y, td->od_w, td->od_h);
    roi_meta->id = od_mtd.id;

    if (!td->add_keypoints)
        return;

    /* Add keypoints group 1 */
    GstAnalyticsGroupMtd group1;
    ret = gst_analytics_relation_meta_add_keypoints_group(
        rmeta, td->semantic_tag, td->dimension, td->positions.size(), td->positions.data(), td->keypoint_count,
        td->confidences.empty() ? nullptr : td->confidences.data(), nullptr, td->skeleton_pairs.size(),
        td->skeleton_pairs.empty() ? nullptr : td->skeleton_pairs.data(), &group1);
    ck_assert_msg(ret == TRUE, "Failed to add keypoints group 1");

    /* OD CONTAINS group */
    gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd.id, group1.id);

    /* Optional second group */
    if (td->add_second_group) {
        GstAnalyticsGroupMtd group2;
        ret = gst_analytics_relation_meta_add_keypoints_group(
            rmeta, td->semantic_tag_2, GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D, td->positions_2.size(),
            td->positions_2.data(), td->keypoint_count_2,
            td->confidences_2.empty() ? nullptr : td->confidences_2.data(), nullptr, td->skeleton_pairs_2.size(),
            td->skeleton_pairs_2.empty() ? nullptr : td->skeleton_pairs_2.data(), &group2);
        ck_assert_msg(ret == TRUE, "Failed to add keypoints group 2");
        gst_analytics_relation_meta_set_relation(rmeta, GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd.id, group2.id);
    }
}

/* ── check: single keypoints group with skeleton ────────────────────────── */

static void check_single_group_with_skeleton(GstBuffer *outbuffer, gpointer user_data) {
    KeypointTestData *td = static_cast<KeypointTestData *>(user_data);
    GstGVAJSONMeta *meta = GST_GVA_JSON_META_GET(outbuffer);
    ck_assert_msg(meta != NULL, "No JSON meta found");
    ck_assert_msg(meta->message != NULL, "No message in meta");

    json msg = json::parse(meta->message);
    ck_assert_msg(msg.contains("objects"), "JSON must contain objects. Message: %s", meta->message);
    ck_assert_msg(msg["objects"].size() == 1, "Expected 1 object. Message: %s", meta->message);

    const json &obj = msg["objects"][0];
    ck_assert_msg(obj.contains("keypoints"), "Object must contain keypoints. Message: %s", meta->message);

    const json &kp_array = obj["keypoints"];
    ck_assert_msg(kp_array.is_array(), "keypoints must be an array. Message: %s", meta->message);
    ck_assert_msg(kp_array.size() == 1, "Expected 1 keypoint group. Message: %s", meta->message);

    const json &kp = kp_array[0];

    /* semantic tag */
    ck_assert_msg(kp.contains("semantic_tag"), "Keypoint group must have semantic_tag. Message: %s", meta->message);
    ck_assert_msg(kp["semantic_tag"] == td->semantic_tag, "Unexpected semantic_tag. Expected %s, got %s. Message: %s",
                  td->semantic_tag, kp["semantic_tag"].get<std::string>().c_str(), meta->message);

    /* points */
    ck_assert_msg(kp.contains("points"), "Keypoint group must have points. Message: %s", meta->message);
    const json &points = kp["points"];
    ck_assert_msg(points.size() == td->keypoint_count, "Expected %zu points, got %zu. Message: %s", td->keypoint_count,
                  points.size(), meta->message);

    /* Verify first point coordinates */
    ck_assert_msg(points[0]["index"] == 0, "First point index must be 0. Message: %s", meta->message);
    ck_assert_msg(points[0]["x"] == td->positions[0], "Unexpected first point x. Message: %s", meta->message);
    ck_assert_msg(points[0]["y"] == td->positions[1], "Unexpected first point y. Message: %s", meta->message);

    /* Verify confidence */
    if (!td->confidences.empty()) {
        assert_json_float(points[0]["confidence"], td->confidences[0], "Unexpected first point confidence");
    }

    /* No descriptor registered for this tag — points must NOT have "name" */
    for (size_t i = 0; i < points.size(); i++) {
        ck_assert_msg(!points[i].contains("name"),
                      "Point %zu must not have 'name' when descriptor is not registered. Message: %s", i,
                      meta->message);
    }

    /* skeleton */
    ck_assert_msg(kp.contains("skeleton"), "Keypoint group must have skeleton. Message: %s", meta->message);
    const json &skeleton = kp["skeleton"];
    ck_assert_msg(skeleton.is_array(), "skeleton must be an array. Message: %s", meta->message);
    ck_assert_msg(skeleton.size() == td->skeleton_pairs.size() / 2, "Expected %zu skeleton edges, got %zu. Message: %s",
                  td->skeleton_pairs.size() / 2, skeleton.size(), meta->message);
}

/* ── check: no keypoints ────────────────────────────────────────────────── */

static void check_no_keypoints(GstBuffer *outbuffer, gpointer user_data) {
    (void)user_data;
    GstGVAJSONMeta *meta = GST_GVA_JSON_META_GET(outbuffer);
    ck_assert_msg(meta != NULL, "No JSON meta found");
    ck_assert_msg(meta->message != NULL, "No message in meta");

    json msg = json::parse(meta->message);
    ck_assert_msg(msg.contains("objects"), "JSON must contain objects. Message: %s", meta->message);
    const json &obj = msg["objects"][0];
    ck_assert_msg(!obj.contains("keypoints"), "Object should NOT contain keypoints when none attached. Message: %s",
                  meta->message);
}

/* ── check: no skeleton ─────────────────────────────────────────────────── */

static void check_no_skeleton(GstBuffer *outbuffer, gpointer user_data) {
    (void)user_data;
    GstGVAJSONMeta *meta = GST_GVA_JSON_META_GET(outbuffer);
    ck_assert_msg(meta != NULL, "No JSON meta found");
    ck_assert_msg(meta->message != NULL, "No message in meta");

    json msg = json::parse(meta->message);
    const json &obj = msg["objects"][0];
    ck_assert_msg(obj.contains("keypoints"), "Object must contain keypoints. Message: %s", meta->message);
    const json &kp = obj["keypoints"][0];
    ck_assert_msg(!kp.contains("skeleton"), "Keypoint group should NOT have skeleton when no edges. Message: %s",
                  meta->message);
    ck_assert_msg(kp.contains("points"), "Keypoint group must have points. Message: %s", meta->message);
}

/* ── check: multiple groups ─────────────────────────────────────────────── */

static void check_multiple_groups(GstBuffer *outbuffer, gpointer user_data) {
    KeypointTestData *td = static_cast<KeypointTestData *>(user_data);
    GstGVAJSONMeta *meta = GST_GVA_JSON_META_GET(outbuffer);
    ck_assert_msg(meta != NULL, "No JSON meta found");
    ck_assert_msg(meta->message != NULL, "No message in meta");

    json msg = json::parse(meta->message);
    const json &obj = msg["objects"][0];
    ck_assert_msg(obj.contains("keypoints"), "Object must contain keypoints. Message: %s", meta->message);

    const json &kp_array = obj["keypoints"];
    ck_assert_msg(kp_array.is_array(), "keypoints must be an array. Message: %s", meta->message);
    ck_assert_msg(kp_array.size() == 2, "Expected 2 keypoint groups, got %zu. Message: %s", kp_array.size(),
                  meta->message);

    /* First group */
    ck_assert_msg(kp_array[0]["semantic_tag"] == td->semantic_tag, "Unexpected first group semantic_tag. Message: %s",
                  meta->message);
    ck_assert_msg(kp_array[0]["points"].size() == td->keypoint_count, "Unexpected first group point count. Message: %s",
                  meta->message);

    /* Second group */
    ck_assert_msg(kp_array[1]["semantic_tag"] == td->semantic_tag_2,
                  "Unexpected second group semantic_tag. Message: %s", meta->message);
    ck_assert_msg(kp_array[1]["points"].size() == td->keypoint_count_2,
                  "Unexpected second group point count. Message: %s", meta->message);
}

/* ── check: 3D keypoints ────────────────────────────────────────────────── */

static void check_3d_keypoints(GstBuffer *outbuffer, gpointer user_data) {
    KeypointTestData *td = static_cast<KeypointTestData *>(user_data);
    GstGVAJSONMeta *meta = GST_GVA_JSON_META_GET(outbuffer);
    ck_assert_msg(meta != NULL, "No JSON meta found");
    ck_assert_msg(meta->message != NULL, "No message in meta");

    json msg = json::parse(meta->message);
    const json &points = msg["objects"][0]["keypoints"][0]["points"];
    ck_assert_msg(points.size() == td->keypoint_count, "Unexpected point count. Message: %s", meta->message);

    /* All 3D points must have z coordinate */
    for (size_t i = 0; i < points.size(); i++) {
        ck_assert_msg(points[i].contains("z"), "3D point %zu must have z coordinate. Message: %s", i, meta->message);
    }

    /* Verify first point */
    ck_assert_msg(points[0]["x"] == td->positions[0], "Unexpected x. Message: %s", meta->message);
    ck_assert_msg(points[0]["y"] == td->positions[1], "Unexpected y. Message: %s", meta->message);
    ck_assert_msg(points[0]["z"] == td->positions[2], "Unexpected z. Message: %s", meta->message);
}

/* ── check: descriptor point names ──────────────────────────────────────── */

static void check_descriptor_point_names(GstBuffer *outbuffer, gpointer user_data) {
    (void)user_data;
    GstGVAJSONMeta *meta = GST_GVA_JSON_META_GET(outbuffer);
    ck_assert_msg(meta != NULL, "No JSON meta found");
    ck_assert_msg(meta->message != NULL, "No message in meta");

    json msg = json::parse(meta->message);
    const json &kp = msg["objects"][0]["keypoints"][0];

    ck_assert_msg(kp["semantic_tag"] == GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5,
                  "Expected face-landmarks/centerface-5 semantic tag. Message: %s", meta->message);

    const json &points = kp["points"];

    /* centerface-5 descriptor has 5 named points */
    const GstAnalyticsKeypointDescriptor *desc =
        gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5);
    ck_assert_msg(desc != NULL, "centerface-5 descriptor not found");
    ck_assert_msg(points.size() == desc->point_count, "Expected %zu points for centerface-5, got %zu. Message: %s",
                  desc->point_count, points.size(), meta->message);

    /* Each point must have a "name" field matching the descriptor */
    for (gsize i = 0; i < desc->point_count; i++) {
        ck_assert_msg(points[i].contains("name"), "Point %zu must have name. Message: %s", i, meta->message);
        ck_assert_msg(points[i]["name"] == desc->point_names[i],
                      "Point %zu name mismatch: expected %s, got %s. Message: %s", i, desc->point_names[i],
                      points[i]["name"].get<std::string>().c_str(), meta->message);
    }

    /* centerface-5 has 0 skeleton connections */
    ck_assert_msg(!kp.contains("skeleton"), "centerface-5 should not have skeleton. Message: %s", meta->message);
}

/* ── test data ──────────────────────────────────────────────────────────── */

/* 3 points, 2 skeleton edges */
static KeypointTestData data_single_group = {
    /* resolution */ {640, 480},
    /* OD bbox */ 100,
    50,
    200,
    300,
    /* confidence */ 0.9,
    /* add_keypoints */ true,
    /* semantic_tag */ "body-pose/test-3",
    /* dimension */ GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D,
    /* positions */ {150, 100, 200, 200, 250, 350},
    /* keypoint_count */ 3,
    /* confidences */ {0.95f, 0.88f, 0.77f},
    /* skeleton_pairs */ {0, 1, 1, 2},
    /* add_second_group */ false,
};

/* No keypoints attached */
static KeypointTestData data_no_keypoints = {
    /* resolution */ {640, 480},
    /* OD bbox */ 100,           50, 200, 300,
    /* confidence */ 0.9,
    /* add_keypoints */ false,
};

/* 2 points, no skeleton */
static KeypointTestData data_no_skeleton = {
    /* resolution */ {640, 480},
    /* OD bbox */ 100,
    50,
    200,
    300,
    /* confidence */ 0.9,
    /* add_keypoints */ true,
    /* semantic_tag */ "test/no-skeleton",
    /* dimension */ GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D,
    /* positions */ {100, 100, 200, 200},
    /* keypoint_count */ 2,
    /* confidences */ {0.9f, 0.8f},
    /* skeleton_pairs */ {},
    /* add_second_group */ false,
};

/* Two groups on same OD */
static KeypointTestData data_multiple_groups = {
    /* resolution */ {640, 480},
    /* OD bbox */ 100,
    50,
    200,
    300,
    /* confidence */ 0.9,
    /* add_keypoints */ true,
    /* group 1 */ "body-pose/test-2",
    GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D,
    /* positions */ {150, 100, 200, 200},
    /* keypoint_count */ 2,
    /* confidences */ {0.9f, 0.8f},
    /* skeleton_pairs */ {0, 1},
    /* add_second_group */ true,
    /* group 2 */ "face-test/3pts",
    /* positions_2 */ {160, 110, 170, 120, 180, 130},
    /* keypoint_count_2 */ 3,
    /* confidences_2 */ {0.7f, 0.6f, 0.5f},
    /* skeleton_pairs_2 */ {},
};

/* 3D keypoints */
static KeypointTestData data_3d_keypoints = {
    /* resolution */ {640, 480},
    /* OD bbox */ 50,
    50,
    100,
    100,
    /* confidence */ 0.85,
    /* add_keypoints */ true,
    /* semantic_tag */ "3d-test/2pts",
    /* dimension */ GST_ANALYTICS_KEYPOINT_DIMENSIONS_3D,
    /* positions */ {10, 20, 30, 40, 50, 60},
    /* keypoint_count */ 2,
    /* confidences */ {0.9f, 0.8f},
    /* skeleton_pairs */ {0, 1},
    /* add_second_group */ false,
};

/* Known descriptor: centerface-5 (5 pts, 0 skeleton edges) */
static KeypointTestData data_centerface5 = {
    /* resolution */ {640, 480},
    /* OD bbox */ 200,
    150,
    80,
    100,
    /* confidence */ 0.95,
    /* add_keypoints */ true,
    /* semantic_tag */ GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5,
    /* dimension */ GST_ANALYTICS_KEYPOINT_DIMENSIONS_2D,
    /* positions */ {220, 170, 260, 170, 240, 190, 225, 220, 255, 220},
    /* keypoint_count */ 5,
    /* confidences */ {0.99f, 0.98f, 0.97f, 0.96f, 0.95f},
    /* skeleton_pairs */ {},
    /* add_second_group */ false,
};

/* ── tests ──────────────────────────────────────────────────────────────── */

GST_START_TEST(test_keypoints_single_group_with_skeleton) {
    g_print("Starting test: test_keypoints_single_group_with_skeleton\n");
    run_test("gvametaconvert", VIDEO_CAPS_TEMPLATE_STRING, data_single_group.resolution, &srctemplate, &sinktemplate,
             setup_keypoint_inbuffer, check_single_group_with_skeleton, &data_single_group, "add-tensor-data", TRUE,
             "source", "test", NULL);
}
GST_END_TEST;

GST_START_TEST(test_keypoints_no_keypoints_on_od) {
    g_print("Starting test: test_keypoints_no_keypoints_on_od\n");
    run_test("gvametaconvert", VIDEO_CAPS_TEMPLATE_STRING, data_no_keypoints.resolution, &srctemplate, &sinktemplate,
             setup_keypoint_inbuffer, check_no_keypoints, &data_no_keypoints, "add-tensor-data", TRUE, "source", "test",
             NULL);
}
GST_END_TEST;

GST_START_TEST(test_keypoints_no_skeleton) {
    g_print("Starting test: test_keypoints_no_skeleton\n");
    run_test("gvametaconvert", VIDEO_CAPS_TEMPLATE_STRING, data_no_skeleton.resolution, &srctemplate, &sinktemplate,
             setup_keypoint_inbuffer, check_no_skeleton, &data_no_skeleton, "add-tensor-data", TRUE, "source", "test",
             NULL);
}
GST_END_TEST;

GST_START_TEST(test_keypoints_multiple_groups) {
    g_print("Starting test: test_keypoints_multiple_groups\n");
    run_test("gvametaconvert", VIDEO_CAPS_TEMPLATE_STRING, data_multiple_groups.resolution, &srctemplate, &sinktemplate,
             setup_keypoint_inbuffer, check_multiple_groups, &data_multiple_groups, "add-tensor-data", TRUE, "source",
             "test", NULL);
}
GST_END_TEST;

GST_START_TEST(test_keypoints_3d) {
    g_print("Starting test: test_keypoints_3d\n");
    run_test("gvametaconvert", VIDEO_CAPS_TEMPLATE_STRING, data_3d_keypoints.resolution, &srctemplate, &sinktemplate,
             setup_keypoint_inbuffer, check_3d_keypoints, &data_3d_keypoints, "add-tensor-data", TRUE, "source", "test",
             NULL);
}
GST_END_TEST;

GST_START_TEST(test_keypoints_descriptor_point_names) {
    g_print("Starting test: test_keypoints_descriptor_point_names\n");
    run_test("gvametaconvert", VIDEO_CAPS_TEMPLATE_STRING, data_centerface5.resolution, &srctemplate, &sinktemplate,
             setup_keypoint_inbuffer, check_descriptor_point_names, &data_centerface5, "add-tensor-data", TRUE,
             "source", "test", NULL);
}
GST_END_TEST;

/* ── suite ──────────────────────────────────────────────────────────────── */

static Suite *metaconvert_keypoints_suite(void) {
    Suite *s = suite_create("metaconvert_keypoints");
    TCase *tc = tcase_create("keypoints");

    suite_add_tcase(s, tc);
    tcase_add_test(tc, test_keypoints_single_group_with_skeleton);
    tcase_add_test(tc, test_keypoints_no_keypoints_on_od);
    tcase_add_test(tc, test_keypoints_no_skeleton);
    tcase_add_test(tc, test_keypoints_multiple_groups);
    tcase_add_test(tc, test_keypoints_3d);
    tcase_add_test(tc, test_keypoints_descriptor_point_names);

    return s;
}

GST_CHECK_MAIN(metaconvert_keypoints);
