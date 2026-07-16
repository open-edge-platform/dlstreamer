/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once
#include <gst/check/gstharness.h>
#include <gst/gst.h>

/* ── Scenario: one test case configuration ─────────────────────────────── */

typedef struct {
    const gchar *label;    /* name used in log / error messages */
    const gchar *out_name; /* golden PNG filename under golden_files/ */
    gint n_cams;           /* 0 = lidar-only (non-batch); ≥1 = batch */
    gint cam_w;            /* synthetic camera frame width  (0 when n_cams==0) */
    gint cam_h;            /* synthetic camera frame height (0 when n_cams==0) */
    gboolean has_3d_dets;  /* attach 3D bounding boxes to lidar buffer */
    gboolean has_2d_dets;  /* attach 2D bounding boxes to camera buffers */
    /* canvas */
    gint width, height;
    /* BEV range (metres) */
    gfloat range_x_min, range_x_max, range_y_min, range_y_max;
    /* point rendering */
    gint point_radius, point_stride;
    gfloat zoom;
    /* view mode: 0=bev  1=perspective  2=cam-proj */
    gint view_mode;
    /* perspective camera */
    gfloat cam_distance, cam_elevation, cam_azimuth, cam_fov;
    /* cam-proj: inject synthetic calibration event */
    gboolean use_calib;
} Scenario;

/* ── LiDAR point-cloud dimensions ──────────────────────────────────────── */
/*
 * 71×71 ground grid + box-shaped objects at each 3D detection (D, E, F).
 * Each 4m×2m×1.5m box contributes 40+40+72+72+45 = 269 surface points.
 */
#define SYNTH_LIDAR_SIDE 71
#define SYNTH_N_LIDAR (SYNTH_LIDAR_SIDE * SYNTH_LIDAR_SIDE) /* 5041 */
#define SYNTH_LIDAR_SPAN 40.0f
#define SYNTH_N_OBJ_PTS (269 * 3)                             /* 807  */
#define SYNTH_N_LIDAR_TOTAL (SYNTH_N_LIDAR + SYNTH_N_OBJ_PTS) /* 5848 */

/* ── Synthetic data generators ─────────────────────────────────────────── */

/*
 * Generate a packed BGR chessboard image (no stride padding).
 * A coloured border (cam_idx 0=red, 1=green, 2=blue) wraps the edge.
 * Caller owns the returned buffer; free with g_free().
 */
guint8 *make_chessboard_bgr(gint w, gint h, gint cam_idx);

/*
 * Generate SYNTH_N_LIDAR_TOTAL LiDAR points (xyzI interleaved, float):
 *   - 71×71 uniform ground grid at z=0, intensity=0.5
 *   - box surface points at D/E/F positions, intensity=1.0
 * Caller owns the returned buffer; free with g_free().
 */
float *make_lidar_pts(void);

/* ── GstHarness setup ──────────────────────────────────────────────────── */

/* Create a harness configured for the given scenario. */
GstHarness *make_lidar_harness(const Scenario *sc);
GstHarness *make_batch_harness(const Scenario *sc);

/* ── Input buffer builders ─────────────────────────────────────────────── */

/*
 * Build the input GstBuffer for one scenario.
 * build_lidar_buf: single lidar stream (non-batch).
 * build_batch_buf: n_cams chessboard cameras + one lidar stream.
 */
GstBuffer *build_lidar_buf(GstHarness *h, const Scenario *sc);
GstBuffer *build_batch_buf(GstHarness *h, const Scenario *sc);

/*
 * Build the g3d/calibration sticky event carrying the synthetic
 * KITTI-extrinsic + synthetic intrinsics used by cam-proj scenarios.
 */
GstEvent *build_calib_event(void);

/*
 * Edge-case buffer builders — used by TC-20/21/22.
 * These tests only assert the element produces a non-NULL output frame;
 * no golden comparison is performed.
 *
 * build_empty_lidar_buf    : 0-point LiDAR buffer (LidarMeta present, count=0)
 * build_no_meta_lidar_buf  : 1-point LiDAR buffer without any LidarMeta
 * build_cam_only_batch_buf : batch buffer with only camera sub-streams (no LiDAR stream)
 */
GstBuffer *build_empty_lidar_buf(GstHarness *h);
GstBuffer *build_no_meta_lidar_buf(GstHarness *h);
GstBuffer *build_cam_only_batch_buf(GstHarness *h, const Scenario *sc);
