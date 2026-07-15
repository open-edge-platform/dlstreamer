/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "g3d_test_synth.h"
#include <cstring>

/* ── camera constants ─────────────────────────────────────────────────── */

#define SYNTH_CAM_SQ     40   /* chessboard square size (pixels) */
#define SYNTH_CAM_BORDER  8   /* coloured border thickness (pixels) */

/* ── make_chessboard_bgr ──────────────────────────────────────────────── */

guint8 *
make_chessboard_bgr(gint w, gint h, gint cam_idx)
{
    static const guint8 BORDER_COLORS[][3] = {
        {  0,   0, 255 },   /* red   (cam 0) */
        {  0, 255,   0 },   /* green (cam 1) */
        {255,   0,   0 },   /* blue  (cam 2) */
    };
    const guint8 *bc = BORDER_COLORS[cam_idx % 3];

    guint8 *bgr = (guint8 *)g_malloc(w * h * 3);
    for (gint y = 0; y < h; y++) {
        for (gint x = 0; x < w; x++) {
            gint off = (y * w + x) * 3;
            if (x < SYNTH_CAM_BORDER || x >= w - SYNTH_CAM_BORDER ||
                y < SYNTH_CAM_BORDER || y >= h - SYNTH_CAM_BORDER) {
                bgr[off]   = bc[0];
                bgr[off+1] = bc[1];
                bgr[off+2] = bc[2];
            } else {
                guint8 v   = ((x / SYNTH_CAM_SQ + y / SYNTH_CAM_SQ) % 2 == 0) ? 255u : 0u;
                bgr[off]   = v;
                bgr[off+1] = v;
                bgr[off+2] = v;
            }
        }
    }
    return bgr;
}

/* ── make_lidar_pts ───────────────────────────────────────────────────── */

float *
make_lidar_pts(void)
{
    float *pts = (float *)g_malloc(SYNTH_N_LIDAR_TOTAL * 4 * sizeof(float));
    gint   idx = 0;

#define ADD_PT(px, py, pz, pi) \
    do { pts[idx*4+0]=(px); pts[idx*4+1]=(py); pts[idx*4+2]=(pz); pts[idx*4+3]=(pi); idx++; } while(0)

    /* ground grid: 71×71 at z=0 spanning ±SYNTH_LIDAR_SPAN m */
    for (gint iy = 0; iy < SYNTH_LIDAR_SIDE; iy++) {
        for (gint ix = 0; ix < SYNTH_LIDAR_SIDE; ix++) {
            float x = -SYNTH_LIDAR_SPAN + ix * (2.f * SYNTH_LIDAR_SPAN / (SYNTH_LIDAR_SIDE - 1));
            float y = -SYNTH_LIDAR_SPAN + iy * (2.f * SYNTH_LIDAR_SPAN / (SYNTH_LIDAR_SIDE - 1));
            ADD_PT(x, y, 0.f, 0.5f);
        }
    }

    /*
     * Box objects at D, E, F — each 4m(x) × 2m(y) × 1.5m(z), yaw=0.
     * Per box: front+back (5y×8z=40 each) + left+right (9x×8z=72 each) + top (9x×5y=45) = 269 pts.
     *
     *   D: centre (17.32,  10,    0) — 30° left  of forward
     *   E: centre (17.32, -10,    0) — 30° right of forward
     *   F: centre (10,    -17.32, 0) — 60° right of forward
     */
#define ADD_BOX(cx, cy) \
    do { \
        for (gint _iz = 0; _iz < 8; _iz++) { \
            for (gint _iy = 0; _iy < 5; _iy++) { \
                float _y = (cy) - 1.f + _iy * 0.5f; \
                float _z = _iz * (1.5f / 7.f); \
                ADD_PT((cx) + 2.f, _y, _z, 1.f); \
                ADD_PT((cx) - 2.f, _y, _z, 1.f); \
            } \
        } \
        for (gint _iz = 0; _iz < 8; _iz++) { \
            for (gint _ix = 0; _ix < 9; _ix++) { \
                float _x = (cx) - 2.f + _ix * 0.5f; \
                float _z = _iz * (1.5f / 7.f); \
                ADD_PT(_x, (cy) + 1.f, _z, 1.f); \
                ADD_PT(_x, (cy) - 1.f, _z, 1.f); \
            } \
        } \
        for (gint _iy = 0; _iy < 5; _iy++) { \
            for (gint _ix = 0; _ix < 9; _ix++) { \
                float _x = (cx) - 2.f + _ix * 0.5f; \
                float _y = (cy) - 1.f + _iy * 0.5f; \
                ADD_PT(_x, _y, 1.5f, 1.f); \
            } \
        } \
    } while (0)

    ADD_BOX(17.32f,   10.0f);
    ADD_BOX(17.32f,  -10.0f);
    ADD_BOX(10.0f,  -17.32f);

#undef ADD_BOX
#undef ADD_PT

    g_assert(idx == SYNTH_N_LIDAR_TOTAL);
    return pts;
}
