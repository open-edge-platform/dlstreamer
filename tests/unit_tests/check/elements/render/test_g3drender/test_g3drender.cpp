/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>

#include "g3d_test_fixtures.h"
#include "g3d_test_golden.h"

#ifndef TEST_FILES_DIR
#define TEST_FILES_DIR ""
#endif

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
    gst_buffer_map(out, &m, GST_MAP_READ);

    gchar *golden_path = g_build_filename(TEST_FILES_DIR, "golden_files", sc->out_name, NULL);

    const gchar *dump_env = g_getenv("G3DRENDER_DUMP_GOLDEN");
    if (dump_env && atoi(dump_env) != 0) {
        save_png(golden_path, m.data, out_w, out_h);
        g_print("  dumped golden -> %s\n", golden_path);
    } else {
        gchar *tmp_path = g_build_filename("/tmp", sc->out_name, NULL);
        save_png(tmp_path, m.data, out_w, out_h);
        g_free(tmp_path);

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

    return s;
}

GST_CHECK_MAIN(g3drender);
