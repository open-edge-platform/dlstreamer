/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once
#include "g3d_test_data.h"
#include <gst/check/gstharness.h>

/* Create a harness configured for the given scenario. */
GstHarness *make_lidar_harness(const Scenario *sc);
GstHarness *make_batch_harness(const Scenario *sc);

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
GstBuffer *build_empty_lidar_buf   (GstHarness *h);
GstBuffer *build_no_meta_lidar_buf (GstHarness *h);
GstBuffer *build_cam_only_batch_buf(GstHarness *h, const Scenario *sc);
