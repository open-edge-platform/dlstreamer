/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once
#include "g3d_test_data.h"

/*
 * Generate a packed BGR chessboard image (no stride padding).
 * A coloured border (cam_idx 0=red, 1=green, 2=blue) wraps the edge.
 * Caller owns the returned buffer; free with g_free().
 */
guint8 *make_chessboard_bgr(gint w, gint h, gint cam_idx);

/*
 * Generate SYNTH_N_LIDAR_TOTAL LiDAR points (xyzI interleaved, float):
 *   - 71×71 uniform ground grid at z=0, intensity=0.5
 *   - box surface points at D/E/F positions,  intensity=1.0
 * Caller owns the returned buffer; free with g_free().
 */
float *make_lidar_pts(void);
