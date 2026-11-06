/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <glib/gstdio.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>

#include <cstring>

#include "g3d_test_fixtures.h"
#include "g3d_test_golden.h"

#ifndef TEST_FILES_DIR
#define TEST_FILES_DIR ""
#endif

static gchar *render_output_path(const gchar *filename) {
    const gchar *output_dir = g_getenv("G3DRENDER_OUTPUT_DIR");
    if (!output_dir || !*output_dir)
        output_dir = g_get_tmp_dir();
    else if (g_mkdir_with_parents(output_dir, 0755) != 0)
        g_error("Failed to create g3drender output directory: %s", output_dir);

    return g_build_filename(output_dir, filename, NULL);
}

/*
 * Synthetic scene: LiDAR X=forward Y=left. Three 3D detections at R=20 m:
 *   D (30°L) — matched to camera box B
 *   E (30°R) — matched to camera box C
 *   F (60°R) — unmatched (outside 640-px camera FOV)
 * Camera: 640×240, f=180, cx=320. Three 2D detections:
 *   A (~58°L) — unmatched   B (~30°L) → D   C (~30°R) → E
 */

static const Scenario SCENARIOS[] = {
    {"pure_lidar", "pure_lidar.png", 0, 0, 0, FALSE, FALSE, 800, 800, -50, 50, -50, 50, 2, 4, 2.0f, 0, 35, 30, 180, 60,
     FALSE},
    {"lidar_with_detections",
     "lidar_with_detections.png",
     0,
     0,
     0,
     TRUE,
     FALSE,
     800,
     800,
     -50,
     50,
     -50,
     50,
     2,
     4,
     2.0f,
     1,
     35,
     30,
     180,
     60,
     FALSE},
    {"batch_single_cam",
     "batch_single_cam.png",
     1,
     640,
     240,
     FALSE,
     FALSE,
     1600,
     800,
     -50,
     50,
     -50,
     50,
     2,
     4,
     2.0f,
     0,
     35,
     30,
     180,
     60,
     FALSE},
    {"batch_single_cam_with_detections",
     "batch_single_cam_with_detections.png",
     1,
     640,
     240,
     TRUE,
     TRUE,
     1600,
     800,
     -50,
     50,
     -50,
     50,
     2,
     4,
     2.0f,
     1,
     35,
     30,
     180,
     60,
     FALSE},
    {"batch_two_cams",
     "batch_two_cams.png",
     2,
     640,
     240,
     FALSE,
     FALSE,
     1600,
     800,
     -50,
     50,
     -50,
     50,
     2,
     4,
     2.0f,
     0,
     35,
     30,
     180,
     60,
     FALSE},
    {"batch_two_cams_with_detections",
     "batch_two_cams_with_detections.png",
     2,
     640,
     240,
     TRUE,
     TRUE,
     1600,
     800,
     -50,
     50,
     -50,
     50,
     2,
     4,
     2.0f,
     1,
     35,
     30,
     180,
     60,
     FALSE},
    /* point-radius=1 exercises the single-pixel fast path instead of cv::circle */
    {"lidar_single_pixel_radius",
     "lidar_single_pixel_radius.png",
     0,
     0,
     0,
     FALSE,
     FALSE,
     800,
     800,
     -50,
     50,
     -50,
     50,
     1,
     4,
     2.0f,
     0,
     35,
     30,
     180,
     60,
     FALSE},
    /* point-stride=1 renders all 5848 points; default stride=4 renders ~1462 */
    {"lidar_dense_stride",
     "lidar_dense_stride.png",
     0,
     0,
     0,
     FALSE,
     FALSE,
     800,
     800,
     -50,
     50,
     -50,
     50,
     2,
     1,
     2.0f,
     0,
     35,
     30,
     180,
     60,
     FALSE},
    /* n_cams=4 switches the camera panel from 1 column to 2×2 grid layout */
    {"batch_four_cams",
     "batch_four_cams.png",
     4,
     640,
     240,
     FALSE,
     FALSE,
     1600,
     800,
     -50,
     50,
     -50,
     50,
     2,
     4,
     2.0f,
     0,
     35,
     30,
     180,
     60,
     FALSE},
    {"batch_three_cams",
     "batch_three_cams.png",
     3,
     640,
     240,
     FALSE,
     FALSE,
     1600,
     800,
     -50,
     50,
     -50,
     50,
     2,
     4,
     2.0f,
     0,
     35,
     30,
     180,
     60,
     FALSE},
};

static const Scenario CAM_PROJ_SCENARIOS[] = {
    {"cam_proj_with_calib",
     "cam_proj_with_calib.png",
     1,
     640,
     240,
     TRUE,
     TRUE,
     1600,
     800,
     -50,
     50,
     -50,
     50,
     2,
     4,
     2.0f,
     2,
     35,
     30,
     180,
     60,
     TRUE},
    {"cam_proj_no_calib",
     "cam_proj_no_calib.png",
     1,
     640,
     240,
     TRUE,
     FALSE,
     1600,
     800,
     -50,
     50,
     -50,
     50,
     2,
     4,
     2.0f,
     2,
     35,
     30,
     180,
     60,
     FALSE},
};

static void run_scenario_impl(const Scenario *sc) {
    g_print("\n[%s]\n", sc->label);

    GstHarness *h = (sc->n_cams == 0) ? make_lidar_harness(sc) : make_batch_harness(sc);
    GstBuffer *buf = (sc->n_cams == 0) ? build_lidar_buf(h, sc) : build_batch_buf(h, sc);

    if (sc->use_calib)
        gst_harness_push_event(h, build_calib_event());

    gst_harness_push(h, buf);
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL, "g3drender produced no output for %s", sc->label);

    gsize out_size = gst_buffer_get_size(out);
    gint out_w = (gint)(out_size / (3 * sc->height));
    gint out_h = sc->height;

    GstMapInfo m;
    ck_assert_msg(gst_buffer_map(out, &m, GST_MAP_READ), "[%s] Failed to map output buffer", sc->label);

    gchar *golden_path = g_build_filename(TEST_FILES_DIR, "golden_files", sc->out_name, NULL);

    const gchar *dump_env = g_getenv("G3DRENDER_DUMP_GOLDEN");
    if (dump_env && atoi(dump_env) != 0) {
        save_png(golden_path, m.data, out_w, out_h);
        g_print("  dumped golden -> %s\n", golden_path);
    } else {
        gchar *output_path = render_output_path(sc->out_name);
        save_png(output_path, m.data, out_w, out_h);
        g_print("  rendered output -> %s\n", output_path);
        g_free(output_path);

        gint gw = 0, gh = 0;
        guint8 *golden = load_png(golden_path, &gw, &gh);
        ck_assert_msg(golden != NULL,
                      "[%s] golden PNG not found: %s\n"
                      "  Run once with G3DRENDER_DUMP_GOLDEN=1 to generate it.",
                      sc->label, golden_path);

        gchar fail_msg[256] = "";
        gint max_diff = compare_with_golden(m.data, out_w, out_h, golden, gw, gh, fail_msg, sizeof(fail_msg));
        g_free(golden);
        g_print("  golden diff: max=%d (threshold=%d)\n", max_diff, MAX_PIXEL_DIFF);
        ck_assert_msg(max_diff >= 0, "[%s] %s", sc->label, fail_msg);
        ck_assert_msg(max_diff <= MAX_PIXEL_DIFF, "[%s] pixel diff exceeds threshold: %s", sc->label, fail_msg);
    }

    g_free(golden_path);
    gst_buffer_unmap(out, &m);
    gst_buffer_unref(out);
    gst_harness_teardown(h);
}

GST_START_TEST(test_render_scenario) {
    run_scenario_impl(&SCENARIOS[__i__]);
}
GST_END_TEST;

GST_START_TEST(test_cam_proj_scenario) {
    run_scenario_impl(&CAM_PROJ_SCENARIOS[__i__]);
}
GST_END_TEST;

static const Scenario SC_EMPTY_POINT_CLOUD = {"empty_point_cloud",
                                              NULL,
                                              0,
                                              0,
                                              0,
                                              FALSE,
                                              FALSE,
                                              800,
                                              800,
                                              -50,
                                              50,
                                              -50,
                                              50,
                                              2,
                                              4,
                                              2.0f,
                                              0,
                                              35,
                                              30,
                                              180,
                                              60,
                                              FALSE};
static const Scenario SC_MISSING_LIDAR_META = {"missing_lidar_meta",
                                               NULL,
                                               0,
                                               0,
                                               0,
                                               FALSE,
                                               FALSE,
                                               800,
                                               800,
                                               -50,
                                               50,
                                               -50,
                                               50,
                                               2,
                                               4,
                                               2.0f,
                                               0,
                                               35,
                                               30,
                                               180,
                                               60,
                                               FALSE};
static const Scenario SC_CAM_ONLY_BATCH = {"cam_only_batch",
                                           NULL,
                                           1,
                                           640,
                                           240,
                                           FALSE,
                                           FALSE,
                                           1600,
                                           800,
                                           -50,
                                           50,
                                           -50,
                                           50,
                                           2,
                                           4,
                                           2.0f,
                                           0,
                                           35,
                                           30,
                                           180,
                                           60,
                                           FALSE};

static const Scenario SC_CAM_PROJ_BG_TUNED = {"cam_proj_bg_tuned",
                                              "cam_proj_bg_tuned.png",
                                              1,
                                              640,
                                              240,
                                              TRUE,
                                              TRUE,
                                              1600,
                                              800,
                                              -50,
                                              50,
                                              -50,
                                              50,
                                              2,
                                              4,
                                              2.0f,
                                              2,
                                              35,
                                              30,
                                              180,
                                              60,
                                              TRUE};

static const Scenario SC_CAM_PROJ_BG_BASELINE = {"cam_proj_bg_baseline",
                                                 "cam_proj_bg_baseline.png",
                                                 1,
                                                 640,
                                                 240,
                                                 TRUE,
                                                 TRUE,
                                                 1600,
                                                 800,
                                                 -50,
                                                 50,
                                                 -50,
                                                 50,
                                                 2,
                                                 4,
                                                 2.0f,
                                                 2,
                                                 35,
                                                 30,
                                                 180,
                                                 60,
                                                 TRUE};

/* bev mode with calibration for both camera pads and class names: the 3D boxes
 * are projected onto each camera tile in per-class colours, and forward-axis=y
 * turns the BEV panel so that sensor +y points up. */
static const Scenario SC_TILES_WITH_CALIB = {"tiles_with_calib",
                                             "tiles_with_calib.png",
                                             2,
                                             640,
                                             240,
                                             TRUE,
                                             TRUE,
                                             1600,
                                             800,
                                             -50,
                                             50,
                                             -50,
                                             50,
                                             2,
                                             4,
                                             2.0f,
                                             0,
                                             35,
                                             30,
                                             180,
                                             60,
                                             TRUE};

GST_START_TEST(test_tiles_with_calib_golden) {
    const Scenario *sc = &SC_TILES_WITH_CALIB;
    g_print("\n[%s]\n", sc->label);

    GstHarness *h = make_batch_harness(sc);
    GstElement *el = gst_harness_find_element(h, "g3drender");
    gst_util_set_object_arg(G_OBJECT(el), "forward-axis", "y");
    gst_object_unref(el);

    static const gchar *const class_names[] = {"car", "truck", NULL};
    GstBuffer *buf = build_batch_buf(h, sc);
    gst_harness_push_event(h, build_calib_event_for_cameras(sc->n_cams, class_names));
    gst_harness_push(h, buf);
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL, "g3drender produced no output for %s", sc->label);

    gsize out_size = gst_buffer_get_size(out);
    gint out_w = (gint)(out_size / (3 * sc->height));
    gint out_h = sc->height;

    GstMapInfo m;
    ck_assert_msg(gst_buffer_map(out, &m, GST_MAP_READ), "[%s] Failed to map output buffer", sc->label);

    gchar *golden_path = g_build_filename(TEST_FILES_DIR, "golden_files", sc->out_name, NULL);
    const gchar *dump_env = g_getenv("G3DRENDER_DUMP_GOLDEN");
    if (dump_env && atoi(dump_env) != 0) {
        save_png(golden_path, m.data, out_w, out_h);
        g_print("  dumped golden -> %s\n", golden_path);
    } else {
        gchar *output_path = render_output_path(sc->out_name);
        save_png(output_path, m.data, out_w, out_h);
        g_print("  rendered output -> %s\n", output_path);
        g_free(output_path);

        gint gw = 0, gh = 0;
        guint8 *golden = load_png(golden_path, &gw, &gh);
        ck_assert_msg(golden != NULL,
                      "[%s] golden PNG not found: %s\n"
                      "  Run once with G3DRENDER_DUMP_GOLDEN=1 to generate it.",
                      sc->label, golden_path);

        gchar fail_msg[256] = "";
        gint max_diff = compare_with_golden(m.data, out_w, out_h, golden, gw, gh, fail_msg, sizeof(fail_msg));
        g_free(golden);
        g_print("  golden diff: max=%d (threshold=%d)\n", max_diff, MAX_PIXEL_DIFF);
        ck_assert_msg(max_diff >= 0, "[%s] %s", sc->label, fail_msg);
        ck_assert_msg(max_diff <= MAX_PIXEL_DIFF, "[%s] pixel diff exceeds threshold: %s", sc->label, fail_msg);
    }

    g_free(golden_path);
    gst_buffer_unmap(out, &m);
    gst_buffer_unref(out);
    gst_harness_teardown(h);
}
GST_END_TEST;

GST_START_TEST(test_empty_point_cloud) {
    GstHarness *h = make_lidar_harness(&SC_EMPTY_POINT_CLOUD);
    GstBuffer *buf = build_empty_lidar_buf(h);
    gst_harness_push(h, buf);
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL, "g3drender produced no output for empty point cloud");
    gst_buffer_unref(out);
    gst_harness_teardown(h);
}
GST_END_TEST;

GST_START_TEST(test_missing_lidar_meta) {
    GstHarness *h = make_lidar_harness(&SC_MISSING_LIDAR_META);
    GstBuffer *buf = build_no_meta_lidar_buf(h);
    gst_harness_push(h, buf);
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL, "g3drender produced no output when LidarMeta is absent");
    gst_buffer_unref(out);
    gst_harness_teardown(h);
}
GST_END_TEST;

GST_START_TEST(test_cam_only_batch) {
    GstHarness *h = make_batch_harness(&SC_CAM_ONLY_BATCH);
    GstBuffer *buf = build_cam_only_batch_buf(h, &SC_CAM_ONLY_BATCH);
    gst_harness_push(h, buf);
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL, "g3drender produced no output when batch has no LiDAR stream");
    gst_buffer_unref(out);
    gst_harness_teardown(h);
}
GST_END_TEST;

GST_START_TEST(test_cam_proj_bg_tuned_golden) {
    const Scenario *sc = &SC_CAM_PROJ_BG_TUNED;
    g_print("\n[%s]\n", sc->label);

    GstHarness *h = make_batch_harness(sc);
    GstElement *el = gst_harness_find_element(h, "g3drender");
    g_object_set(G_OBJECT(el), "cam-bg-grayscale", FALSE, "cam-bg-dim", 1.0f, NULL);
    gst_object_unref(el);

    GstBuffer *buf = build_batch_buf(h, sc);
    gst_harness_push_event(h, build_calib_event());
    gst_harness_push(h, buf);
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL, "g3drender produced no output for %s", sc->label);

    gsize out_size = gst_buffer_get_size(out);
    gint out_w = (gint)(out_size / (3 * sc->height));
    gint out_h = sc->height;

    GstMapInfo m;
    ck_assert_msg(gst_buffer_map(out, &m, GST_MAP_READ), "[%s] Failed to map output buffer", sc->label);

    gchar *golden_path = g_build_filename(TEST_FILES_DIR, "golden_files", sc->out_name, NULL);
    const gchar *dump_env = g_getenv("G3DRENDER_DUMP_GOLDEN");
    if (dump_env && atoi(dump_env) != 0) {
        save_png(golden_path, m.data, out_w, out_h);
        g_print("  dumped golden -> %s\n", golden_path);
    } else {
        gchar *output_path = render_output_path(sc->out_name);
        save_png(output_path, m.data, out_w, out_h);
        g_print("  rendered output -> %s\n", output_path);
        g_free(output_path);

        gint gw = 0, gh = 0;
        guint8 *golden = load_png(golden_path, &gw, &gh);
        ck_assert_msg(golden != NULL,
                      "[%s] golden PNG not found: %s\n"
                      "  Run once with G3DRENDER_DUMP_GOLDEN=1 to generate it.",
                      sc->label, golden_path);

        gchar fail_msg[256] = "";
        gint max_diff = compare_with_golden(m.data, out_w, out_h, golden, gw, gh, fail_msg, sizeof(fail_msg));
        g_free(golden);
        g_print("  golden diff: max=%d (threshold=%d)\n", max_diff, MAX_PIXEL_DIFF);
        ck_assert_msg(max_diff >= 0, "[%s] %s", sc->label, fail_msg);
        ck_assert_msg(max_diff <= MAX_PIXEL_DIFF, "[%s] pixel diff exceeds threshold: %s", sc->label, fail_msg);
    }

    g_free(golden_path);
    gst_buffer_unmap(out, &m);
    gst_buffer_unref(out);
    gst_harness_teardown(h);
}
GST_END_TEST;

GST_START_TEST(test_cam_proj_bg_baseline_golden) {
    const Scenario *sc = &SC_CAM_PROJ_BG_BASELINE;
    g_print("\n[%s]\n", sc->label);

    GstHarness *h = make_batch_harness(sc);
    GstElement *el = gst_harness_find_element(h, "g3drender");
    g_object_set(G_OBJECT(el), "cam-bg-grayscale", TRUE, "cam-bg-dim", 0.65f, NULL);
    gst_object_unref(el);

    GstBuffer *buf = build_batch_buf(h, sc);
    gst_harness_push_event(h, build_calib_event());
    gst_harness_push(h, buf);
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL, "g3drender produced no output for %s", sc->label);

    gsize out_size = gst_buffer_get_size(out);
    gint out_w = (gint)(out_size / (3 * sc->height));
    gint out_h = sc->height;

    GstMapInfo m;
    ck_assert_msg(gst_buffer_map(out, &m, GST_MAP_READ), "[%s] Failed to map output buffer", sc->label);

    gchar *golden_path = g_build_filename(TEST_FILES_DIR, "golden_files", sc->out_name, NULL);
    const gchar *dump_env = g_getenv("G3DRENDER_DUMP_GOLDEN");
    if (dump_env && atoi(dump_env) != 0) {
        save_png(golden_path, m.data, out_w, out_h);
        g_print("  dumped golden -> %s\n", golden_path);
    } else {
        gchar *output_path = render_output_path(sc->out_name);
        save_png(output_path, m.data, out_w, out_h);
        g_print("  rendered output -> %s\n", output_path);
        g_free(output_path);

        gint gw = 0, gh = 0;
        guint8 *golden = load_png(golden_path, &gw, &gh);
        ck_assert_msg(golden != NULL,
                      "[%s] golden PNG not found: %s\n"
                      "  Run once with G3DRENDER_DUMP_GOLDEN=1 to generate it.",
                      sc->label, golden_path);

        gchar fail_msg[256] = "";
        gint max_diff = compare_with_golden(m.data, out_w, out_h, golden, gw, gh, fail_msg, sizeof(fail_msg));
        g_free(golden);
        g_print("  golden diff: max=%d (threshold=%d)\n", max_diff, MAX_PIXEL_DIFF);
        ck_assert_msg(max_diff >= 0, "[%s] %s", sc->label, fail_msg);
        ck_assert_msg(max_diff <= MAX_PIXEL_DIFF, "[%s] pixel diff exceeds threshold: %s", sc->label, fail_msg);
    }

    g_free(golden_path);
    gst_buffer_unmap(out, &m);
    gst_buffer_unref(out);
    gst_harness_teardown(h);
}
GST_END_TEST;

/* Two cameras in cam-proj, with 3D boxes, so both the points and the boxes are projected. */
static const Scenario SC_CAM_PROJ_TWO_CAMS = {"cam_proj_two_cams",
                                              NULL,
                                              2,
                                              640,
                                              240,
                                              TRUE,
                                              TRUE,
                                              1600,
                                              800,
                                              -50,
                                              50,
                                              -50,
                                              50,
                                              2,
                                              4,
                                              2.0f,
                                              2,
                                              35,
                                              30,
                                              180,
                                              60,
                                              TRUE};

/* Camera 1's principal point sits this far right of camera 0's, so projecting camera 1 with
 * camera 0's matrices moves every point and box in the frame. */
#define CAM1_CX_SHIFT 60.f

static GstBuffer *render_frame(GstHarness *h, const Scenario *sc) {
    gst_harness_push(h, build_batch_buf(h, sc));
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL, "g3drender produced no output for %s", sc->label);
    return out;
}

static void set_cam_proj_index(GstHarness *h, gint index) {
    GstElement *el = gst_harness_find_element(h, "g3drender");
    g_object_set(el, "cam-proj-index", index, NULL);
    gst_object_unref(el);
}

/* One frame of the camera on pad @cam_index after a calibration event with @n_entries cameras. */
static GstBuffer *render_with_calibration(const Scenario *sc, gint cam_index, const gfloat *cx_shift, gint n_entries) {
    GstHarness *h = make_batch_harness(sc);
    set_cam_proj_index(h, cam_index);
    gst_harness_push_event(h, build_calib_event_shifted(cx_shift, n_entries));
    GstBuffer *out = render_frame(h, sc);
    gst_harness_teardown(h);
    return out;
}

static gboolean same_frame(GstBuffer *a, GstBuffer *b) {
    GstMapInfo mb;
    if (gst_buffer_get_size(a) != gst_buffer_get_size(b) || !gst_buffer_map(b, &mb, GST_MAP_READ))
        return FALSE;
    gboolean same = gst_buffer_memcmp(a, 0, mb.data, mb.size) == 0;
    gst_buffer_unmap(b, &mb);
    return same;
}

/* Camera 1 is projected with the camera-1 entry alone: changing camera 0's entry leaves the
 * frame as it was, and giving camera 1 camera 0's matrices changes it. */
GST_START_TEST(test_cam_proj_uses_camera_calibration) {
    const Scenario *sc = &SC_CAM_PROJ_TWO_CAMS;
    const gfloat own[] = {0.f, CAM1_CX_SHIFT};
    const gfloat other_cam0[] = {CAM1_CX_SHIFT, CAM1_CX_SHIFT};
    const gfloat cam0_matrices[] = {0.f, 0.f};

    GstBuffer *frame = render_with_calibration(sc, 1, own, 2);
    GstBuffer *cam0_changed = render_with_calibration(sc, 1, other_cam0, 2);
    GstBuffer *with_cam0 = render_with_calibration(sc, 1, cam0_matrices, 2);
    ck_assert_msg(same_frame(frame, cam0_changed), "camera 1's projection depends on the camera-0 entry");
    ck_assert_msg(!same_frame(frame, with_cam0), "camera 1 was not projected with its camera-1 calibration");
    gst_buffer_unref(frame);
    gst_buffer_unref(cam0_changed);
    gst_buffer_unref(with_cam0);
}
GST_END_TEST;

/* Switching cam-proj-index after the calibration arrived switches the matrices with the image. */
GST_START_TEST(test_cam_proj_index_switch_at_runtime) {
    const Scenario *sc = &SC_CAM_PROJ_TWO_CAMS;
    const gfloat both[] = {0.f, CAM1_CX_SHIFT};

    GstHarness *h = make_batch_harness(sc);
    gst_harness_push_event(h, build_calib_event_shifted(both, 2));
    GstBuffer *first = render_frame(h, sc);
    set_cam_proj_index(h, 1);
    GstBuffer *switched = render_frame(h, sc);
    gst_harness_teardown(h);

    GstBuffer *expected = render_with_calibration(sc, 1, both, 2);
    ck_assert_msg(!same_frame(first, switched), "switching cam-proj-index did not change the frame");
    ck_assert_msg(same_frame(switched, expected), "after switching, camera 1 is not drawn as it is from the start");
    gst_buffer_unref(first);
    gst_buffer_unref(switched);
    gst_buffer_unref(expected);
}
GST_END_TEST;

/* A later g3d/calibration event (g3dobjectfuser reloaded its file) replaces the earlier one. */
GST_START_TEST(test_cam_proj_calibration_replaced) {
    const Scenario *sc = &SC_CAM_PROJ_TWO_CAMS;
    const gfloat original[] = {0.f, 0.f};
    const gfloat reloaded[] = {0.f, CAM1_CX_SHIFT};

    GstHarness *h = make_batch_harness(sc);
    set_cam_proj_index(h, 1);
    gst_harness_push_event(h, build_calib_event_shifted(original, 2));
    GstBuffer *before = render_frame(h, sc);
    gst_harness_push_event(h, build_calib_event_shifted(reloaded, 2));
    GstBuffer *after = render_frame(h, sc);
    gst_harness_teardown(h);

    GstBuffer *expected = render_with_calibration(sc, 1, reloaded, 2);
    ck_assert_msg(!same_frame(before, after), "the new calibration event was ignored");
    ck_assert_msg(same_frame(after, expected), "the frame does not use the new calibration");
    gst_buffer_unref(before);
    gst_buffer_unref(after);
    gst_buffer_unref(expected);
}
GST_END_TEST;

/* 802 BGR pixels are 2406 bytes, not a multiple of 4. */
#define UNALIGNED_WIDTH 802
#define UNALIGNED_HEIGHT 400

/* A width whose BGR rows are not a multiple of 4 bytes: the frame has GStreamer's default
 * stride, padded to 4 bytes, so its size matches the caps and videoconvert reads the same
 * picture instead of rejecting the buffer. */
GST_START_TEST(test_unaligned_width) {
    const Scenario sc = {"unaligned_width",
                         NULL,
                         0,
                         0,
                         0,
                         TRUE,
                         FALSE,
                         UNALIGNED_WIDTH,
                         UNALIGNED_HEIGHT,
                         -40,
                         40,
                         -40,
                         40,
                         2,
                         1,
                         1.0f,
                         0,
                         35,
                         30,
                         180,
                         60,
                         FALSE};
    const gchar *props = "width=802 height=400 point-radius=2 point-stride=1";

    gchar *desc = g_strdup_printf("g3drender %s", props);
    GstHarness *h = gst_harness_new_parse(desc);
    g_free(desc);
    gst_harness_set_src_caps_str(h, "application/x-lidar");
    ck_assert_int_eq(gst_harness_push(h, build_lidar_buf(h, &sc)), GST_FLOW_OK);
    GstBuffer *bgr = gst_harness_pull(h);
    ck_assert(bgr != NULL);
    GstCaps *caps = gst_pad_get_current_caps(h->sinkpad);
    GstVideoInfo info;
    ck_assert(gst_video_info_from_caps(&info, caps));
    gst_caps_unref(caps);
    ck_assert_int_eq(GST_VIDEO_INFO_WIDTH(&info), UNALIGNED_WIDTH);
    ck_assert_int_eq(GST_VIDEO_INFO_PLANE_STRIDE(&info, 0), GST_ROUND_UP_4(UNALIGNED_WIDTH * 3));
    ck_assert_uint_eq(gst_buffer_get_size(bgr), GST_VIDEO_INFO_SIZE(&info));
    gst_harness_teardown(h);

    desc = g_strdup_printf("g3drender %s ! videoconvert ! capsfilter caps=video/x-raw,format=BGRx", props);
    h = gst_harness_new_parse(desc);
    g_free(desc);
    gst_harness_set_src_caps_str(h, "application/x-lidar");
    ck_assert_int_eq(gst_harness_push(h, build_lidar_buf(h, &sc)), GST_FLOW_OK);
    GstBuffer *bgrx = gst_harness_pull(h);
    ck_assert(bgrx != NULL);
    gst_harness_teardown(h);

    GstMapInfo a, b;
    ck_assert(gst_buffer_map(bgr, &a, GST_MAP_READ));
    ck_assert(gst_buffer_map(bgrx, &b, GST_MAP_READ));
    const gint bgr_stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);
    const gint bgrx_stride = UNALIGNED_WIDTH * 4;
    ck_assert_uint_ge(b.size, (gsize)bgrx_stride * UNALIGNED_HEIGHT);
    gint differing = 0;
    for (gint y = 0; y < UNALIGNED_HEIGHT; y++)
        for (gint x = 0; x < UNALIGNED_WIDTH; x++)
            differing += memcmp(a.data + y * bgr_stride + x * 3, b.data + y * bgrx_stride + x * 4, 3) != 0;
    gst_buffer_unmap(bgr, &a);
    gst_buffer_unmap(bgrx, &b);
    gst_buffer_unref(bgr);
    gst_buffer_unref(bgrx);
    ck_assert_msg(differing == 0, "%d pixels differ after videoconvert", differing);
}
GST_END_TEST;

static Suite *g3drender_suite(void) {
    Suite *s = suite_create("g3drender");

    TCase *tc = tcase_create("fixture");
    tcase_add_loop_test(tc, test_render_scenario, 0, G_N_ELEMENTS(SCENARIOS));
    suite_add_tcase(s, tc);

    TCase *tc_proj = tcase_create("cam-proj");
    tcase_add_loop_test(tc_proj, test_cam_proj_scenario, 0, G_N_ELEMENTS(CAM_PROJ_SCENARIOS));
    suite_add_tcase(s, tc_proj);

    TCase *tc_edge = tcase_create("edge-cases");
    tcase_add_test(tc_edge, test_empty_point_cloud);
    tcase_add_test(tc_edge, test_missing_lidar_meta);
    tcase_add_test(tc_edge, test_cam_only_batch);
    suite_add_tcase(s, tc_edge);

    TCase *tc_proj_bg = tcase_create("cam-proj-bg");
    tcase_add_test(tc_proj_bg, test_cam_proj_bg_tuned_golden);
    tcase_add_test(tc_proj_bg, test_cam_proj_bg_baseline_golden);
    suite_add_tcase(s, tc_proj_bg);

    TCase *tc_tiles = tcase_create("tiles-calib");
    tcase_add_test(tc_tiles, test_tiles_with_calib_golden);
    suite_add_tcase(s, tc_tiles);

    TCase *tc_proj_calib = tcase_create("cam-proj-calibration");
    tcase_add_test(tc_proj_calib, test_cam_proj_uses_camera_calibration);
    tcase_add_test(tc_proj_calib, test_cam_proj_index_switch_at_runtime);
    tcase_add_test(tc_proj_calib, test_cam_proj_calibration_replaced);
    suite_add_tcase(s, tc_proj_calib);

    TCase *tc_layout = tcase_create("output-layout");
    tcase_add_test(tc_layout, test_unaligned_width);
    suite_add_tcase(s, tc_layout);

    return s;
}

GST_CHECK_MAIN(g3drender);
