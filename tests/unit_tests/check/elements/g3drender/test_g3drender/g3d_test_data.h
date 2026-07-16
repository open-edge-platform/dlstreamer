/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once
#include <gst/gst.h>

/* ── Scenario: one test case configuration ─────────────────────────────── */

typedef struct {
    const gchar *label;      /* name used in log / error messages */
    const gchar *out_name;   /* golden PNG filename under golden_files/ */
    gint         n_cams;     /* 0 = lidar-only (non-batch); ≥1 = batch */
    gint         cam_w;      /* synthetic camera frame width  (0 when n_cams==0) */
    gint         cam_h;      /* synthetic camera frame height (0 when n_cams==0) */
    gboolean     has_3d_dets;/* attach 3D bounding boxes to lidar buffer */
    gboolean     has_2d_dets;/* attach 2D bounding boxes to camera buffers */
    /* canvas */
    gint   width, height;
    /* BEV range (metres) */
    gfloat range_x_min, range_x_max, range_y_min, range_y_max;
    /* point rendering */
    gint   point_radius, point_stride;
    gfloat zoom;
    /* view mode: 0=bev  1=perspective  2=cam-proj */
    gint   view_mode;
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
#define SYNTH_LIDAR_SIDE    71
#define SYNTH_N_LIDAR       (SYNTH_LIDAR_SIDE * SYNTH_LIDAR_SIDE) /* 5041 */
#define SYNTH_LIDAR_SPAN    40.0f
#define SYNTH_N_OBJ_PTS     (269 * 3)                             /* 807  */
#define SYNTH_N_LIDAR_TOTAL (SYNTH_N_LIDAR + SYNTH_N_OBJ_PTS)    /* 5848 */
