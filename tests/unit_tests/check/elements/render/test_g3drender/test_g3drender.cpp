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

/* ── scenario tables ──────────────────────────────────────────────────── */
/*
 * Scene: LiDAR X=forward Y=left.  Three 3D detections at R=20 m:
 *   D (30°L) — matched to camera box B
 *   E (30°R) — matched to camera box C
 *   F (60°R) — unmatched (outside 640-px camera FOV)
 * Camera: 640×240, f=180, cx=320; three 2D detections:
 *   A (~58°L) — unmatched   B (~30°L) → D   C (~30°R) → E
 */

/*                                                              ncams camW camH 3ddet 2ddet  W     H    xmin xmax ymin ymax  rad str  zoom  mode dist elev  az   fov  calib */
static const Scenario SCENARIOS[] = {
    { "01_pure_lidar",           "01_pure_lidar.png",           0, 0,   0,   FALSE, FALSE, 800,  800, -50, 50, -50, 50, 2, 4, 2.0f, 0, 35, 30, 180, 60, FALSE },
    { "02_lidar_inference",      "02_lidar_inference.png",      0, 0,   0,   TRUE,  FALSE, 800,  800, -50, 50, -50, 50, 2, 4, 2.0f, 1, 35, 30, 180, 60, FALSE },
    { "03_batch_1cam",           "03_batch_1cam.png",           1, 640, 240, FALSE, FALSE, 1600, 800, -50, 50, -50, 50, 2, 4, 2.0f, 0, 35, 30, 180, 60, FALSE },
    { "04_batch_1cam_inference", "04_batch_1cam_inference.png", 1, 640, 240, TRUE,  TRUE,  1600, 800, -50, 50, -50, 50, 2, 4, 2.0f, 1, 35, 30, 180, 60, FALSE },
    { "05_batch_2cam",           "05_batch_2cam.png",           2, 640, 240, FALSE, FALSE, 1600, 800, -50, 50, -50, 50, 2, 4, 2.0f, 0, 35, 30, 180, 60, FALSE },
    { "06_batch_2cam_inference", "06_batch_2cam_inference.png", 2, 640, 240, TRUE,  TRUE,  1600, 800, -50, 50, -50, 50, 2, 4, 2.0f, 1, 35, 30, 180, 60, FALSE },
    /* TC-09: point_radius=1 — exercises the single-pixel fast path in draw_bev / draw_perspective
     *        instead of cv::circle; produces a visually denser but lower-weight render. */
    { "09_lidar_radius1",        "09_lidar_radius1.png",        0, 0,   0,   FALSE, FALSE, 800,  800, -50, 50, -50, 50, 1, 4, 2.0f, 0, 35, 30, 180, 60, FALSE },
    /* TC-10: point_stride=1 — renders every point (5848 pts) vs TC-01's stride=4 (~1462 pts);
     *        verifies the dense path produces no crash and a visually saturated point cloud. */
    { "10_lidar_stride1",        "10_lidar_stride1.png",        0, 0,   0,   FALSE, FALSE, 800,  800, -50, 50, -50, 50, 2, 1, 2.0f, 0, 35, 30, 180, 60, FALSE },
    /* TC-25: 4 cameras — n_cams>=4 switches layout to 2 cols x 2 rows; each cell 400x400.
     *        640x240 cam in 400x400 cell: scale=0.625 → sw=400 sh=150, y-pad=125 top+bottom. */
    { "25_batch_4cam",           "25_batch_4cam.png",           4, 640, 240, FALSE, FALSE, 1600, 800, -50, 50, -50, 50, 2, 4, 2.0f, 0, 35, 30, 180, 60, FALSE },
    /* TC-31: 3 cameras — 1 col x 3 rows, each cell 800x266.
     *        640x240 cam in 800x266 cell: scale=min(1.25,1.108)=1.108 → sw=709 sh=266, x-pad=45. */
    { "31_batch_3cam",           "31_batch_3cam.png",           3, 640, 240, FALSE, FALSE, 1600, 800, -50, 50, -50, 50, 2, 4, 2.0f, 0, 35, 30, 180, 60, FALSE },
};

static const Scenario CAM_PROJ_SCENARIOS[] = {
    { "07_batch_1cam_project",         "07_batch_1cam_project.png",         1, 640, 240, TRUE, TRUE,  1600, 800, -50, 50, -50, 50, 2, 4, 2.0f, 2, 35, 30, 180, 60, TRUE  },
    { "08_batch_1cam_project_nocalib", "08_batch_1cam_project_nocalib.png", 1, 640, 240, TRUE, FALSE, 1600, 800, -50, 50, -50, 50, 2, 4, 2.0f, 2, 35, 30, 180, 60, FALSE },
};

/* ── test logic ───────────────────────────────────────────────────────── */

static void
run_scenario_impl(const Scenario *sc)
{
    g_print("\n[%s]\n", sc->label);

    GstHarness *h   = (sc->n_cams == 0) ? make_lidar_harness(sc) : make_batch_harness(sc);
    GstBuffer  *buf = (sc->n_cams == 0) ? build_lidar_buf(h, sc) : build_batch_buf(h, sc);

    if (sc->use_calib)
        gst_harness_push_event(h, build_calib_event());

    gst_harness_push(h, buf);
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL, "g3drender produced no output for %s", sc->label);

    gsize out_size = gst_buffer_get_size(out);
    gint  out_w    = (gint)(out_size / (3 * sc->height));
    gint  out_h    = sc->height;

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

        gint    gw = 0, gh = 0;
        guint8 *golden = load_png(golden_path, &gw, &gh);
        ck_assert_msg(golden != NULL,
                      "[%s] golden PNG not found: %s\n"
                      "  Run once with G3DRENDER_DUMP_GOLDEN=1 to generate it.",
                      sc->label, golden_path);

        gchar fail_msg[256] = "";
        gint  max_diff = compare_with_golden(m.data, out_w, out_h,
                                             golden, gw, gh,
                                             fail_msg, sizeof(fail_msg));
        g_free(golden);
        g_print("  golden diff: max=%d (threshold=%d)\n", max_diff, MAX_PIXEL_DIFF);
        ck_assert_msg(max_diff >= 0,          "[%s] %s", sc->label, fail_msg);
        ck_assert_msg(max_diff <= MAX_PIXEL_DIFF,
                      "[%s] pixel diff exceeds threshold: %s", sc->label, fail_msg);
    }

    g_free(golden_path);
    gst_buffer_unmap(out, &m);
    gst_buffer_unref(out);
    gst_harness_teardown(h);
}

/* ── test cases ───────────────────────────────────────────────────────── */

GST_START_TEST(test_render_scenario)
{
    run_scenario_impl(&SCENARIOS[__i__]);
}
GST_END_TEST;

GST_START_TEST(test_cam_proj_scenario)
{
    run_scenario_impl(&CAM_PROJ_SCENARIOS[__i__]);
}
GST_END_TEST;

/* ── edge-case test cases (TC-20 / TC-21 / TC-22) ────────────────────── */
/*
 * These tests verify the element does not crash and always produces a
 * valid output buffer.  No golden comparison is performed — the inputs
 * represent degenerate or partially-missing data.
 */

/*                                                                  ncams camW camH 3ddet 2ddet  W     H    xmin xmax ymin ymax  rad str  zoom  mode dist elev  az   fov  calib */
static const Scenario SC_EMPTY_LIDAR_PTS  = {
    "20_empty_lidar_pts",   NULL, 0, 0,   0,   FALSE, FALSE, 800,  800, -50, 50, -50, 50, 2, 4, 2.0f, 0, 35, 30, 180, 60, FALSE };
static const Scenario SC_NO_LIDAR_META    = {
    "21_no_lidar_meta",     NULL, 0, 0,   0,   FALSE, FALSE, 800,  800, -50, 50, -50, 50, 2, 4, 2.0f, 0, 35, 30, 180, 60, FALSE };
static const Scenario SC_NO_LIDAR_STREAM  = {
    "22_no_lidar_stream",   NULL, 1, 640, 240, FALSE, FALSE, 1600, 800, -50, 50, -50, 50, 2, 4, 2.0f, 0, 35, 30, 180, 60, FALSE };

/* TC-20: 0-point LiDAR (LidarMeta present, count=0) */
GST_START_TEST(test_empty_lidar_pts)
{
    g_print("\n[20_empty_lidar_pts] LiDAR buffer with 0 points — must output valid frame\n");
    GstHarness *h   = make_lidar_harness(&SC_EMPTY_LIDAR_PTS);
    GstBuffer  *buf = build_empty_lidar_buf(h);
    gst_harness_push(h, buf);
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL,
                  "g3drender produced no output for empty point cloud (count=0)");
    gst_buffer_unref(out);
    gst_harness_teardown(h);
}
GST_END_TEST;

/* TC-21: LiDAR buffer has no LidarMeta attached */
GST_START_TEST(test_no_lidar_meta)
{
    g_print("\n[21_no_lidar_meta] LiDAR buffer without LidarMeta — must output blank frame\n");
    GstHarness *h   = make_lidar_harness(&SC_NO_LIDAR_META);
    GstBuffer  *buf = build_no_meta_lidar_buf(h);
    gst_harness_push(h, buf);
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL,
                  "g3drender produced no output when LidarMeta is absent");
    gst_buffer_unref(out);
    gst_harness_teardown(h);
}
GST_END_TEST;

/* TC-22: Batch buffer contains only camera sub-streams — no LiDAR sub-stream */
GST_START_TEST(test_batch_no_lidar_stream)
{
    g_print("\n[22_no_lidar_stream] Batch with no LiDAR sub-stream — camera panel must still render\n");
    GstHarness *h   = make_batch_harness(&SC_NO_LIDAR_STREAM);
    GstBuffer  *buf = build_cam_only_batch_buf(h, &SC_NO_LIDAR_STREAM);
    gst_harness_push(h, buf);
    GstBuffer *out = gst_harness_pull(h);
    ck_assert_msg(out != NULL,
                  "g3drender produced no output when batch has no LiDAR sub-stream");
    gst_buffer_unref(out);
    gst_harness_teardown(h);
}
GST_END_TEST;

/* ── suite ────────────────────────────────────────────────────────────── */

static Suite *
g3drender_suite(void)
{
    Suite *s = suite_create("g3drender");

    TCase *tc = tcase_create("fixture");
    tcase_add_loop_test(tc, test_render_scenario, 0, G_N_ELEMENTS(SCENARIOS));
    suite_add_tcase(s, tc);

    TCase *tc_proj = tcase_create("cam-proj");
    tcase_add_loop_test(tc_proj, test_cam_proj_scenario, 0, G_N_ELEMENTS(CAM_PROJ_SCENARIOS));
    suite_add_tcase(s, tc_proj);

    TCase *tc_edge = tcase_create("edge-cases");
    tcase_add_test(tc_edge, test_empty_lidar_pts);
    tcase_add_test(tc_edge, test_no_lidar_meta);
    tcase_add_test(tc_edge, test_batch_no_lidar_stream);
    suite_add_tcase(s, tc_edge);

    return s;
}

GST_CHECK_MAIN(g3drender);
