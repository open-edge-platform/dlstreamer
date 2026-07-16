/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once
#include <gst/check/gstharness.h>
#include <gst/gst.h>

typedef struct {
    const gchar *label;
    const gchar *out_name;
    gint n_cams;          /* 0 = lidar-only (non-batch); ≥1 = batch */
    gint cam_w;
    gint cam_h;
    gboolean has_3d_dets;
    gboolean has_2d_dets;
    gint width, height;
    gfloat range_x_min, range_x_max, range_y_min, range_y_max;
    gint point_radius, point_stride;
    gfloat zoom;
    gint view_mode;       /* 0=bev  1=perspective  2=cam-proj */
    gfloat cam_distance, cam_elevation, cam_azimuth, cam_fov;
    gboolean use_calib;   /* inject synthetic g3d/calibration sticky event */
} Scenario;

/*
 * 71×71 ground grid + box surface points at each 3D detection (D, E, F).
 * Each 4m×2m×1.5m box contributes 269 points. Total: 5041 + 807 = 5848.
 */
#define SYNTH_LIDAR_SIDE 71
#define SYNTH_N_LIDAR (SYNTH_LIDAR_SIDE * SYNTH_LIDAR_SIDE) /* 5041 */
#define SYNTH_LIDAR_SPAN 40.0f
#define SYNTH_N_OBJ_PTS (269 * 3)                             /* 807  */
#define SYNTH_N_LIDAR_TOTAL (SYNTH_N_LIDAR + SYNTH_N_OBJ_PTS) /* 5848 */

guint8 *make_chessboard_bgr(gint w, gint h, gint cam_idx);
float *make_lidar_pts(void);

GstHarness *make_lidar_harness(const Scenario *sc);
GstHarness *make_batch_harness(const Scenario *sc);

GstBuffer *build_lidar_buf(GstHarness *h, const Scenario *sc);
GstBuffer *build_batch_buf(GstHarness *h, const Scenario *sc);
GstEvent *build_calib_event(void);

GstBuffer *build_empty_lidar_buf(GstHarness *h);
GstBuffer *build_no_meta_lidar_buf(GstHarness *h);
GstBuffer *build_cam_only_batch_buf(GstHarness *h, const Scenario *sc);
