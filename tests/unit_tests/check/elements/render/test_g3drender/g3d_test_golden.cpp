/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "g3d_test_golden.h"
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

void
save_png(const gchar *path, const guint8 *bgr, gint w, gint h)
{
    /* cv::imwrite expects BGR, which matches g3drender's output format. */
    cv::Mat mat(h, w, CV_8UC3, (void *)bgr);
    if (!cv::imwrite(path, mat))
        g_warning("save_png: failed to write %s", path);
    else
        g_print("  saved -> %s\n", path);
}

guint8 *
load_png(const gchar *path, gint *w_out, gint *h_out)
{
    /* cv::imread returns BGR, matching g3drender's output format. */
    cv::Mat mat = cv::imread(path, cv::IMREAD_COLOR);
    if (mat.empty())
        return NULL;
    *w_out = mat.cols;
    *h_out = mat.rows;
    gsize   n    = (gsize)mat.cols * mat.rows * 3;
    guint8 *data = (guint8 *)g_memdup2(mat.data, n);
    return data;
}

gint
compare_with_golden(const guint8 *bgr,        gint w,  gint h,
                    const guint8 *golden_bgr,  gint gw, gint gh,
                    gchar        *fail_msg,    gsize fail_msg_len)
{
    if (w != gw || h != gh) {
        g_snprintf(fail_msg, fail_msg_len,
                   "size mismatch: render=%dx%d golden=%dx%d", w, h, gw, gh);
        return -1;
    }
    gint  max_diff = 0;
    gsize worst    = 0;
    for (gsize i = 0; i < (gsize)w * h; i++) {
        gint db = abs((gint)bgr[i*3+0] - (gint)golden_bgr[i*3+0]);
        gint dg = abs((gint)bgr[i*3+1] - (gint)golden_bgr[i*3+1]);
        gint dr = abs((gint)bgr[i*3+2] - (gint)golden_bgr[i*3+2]);
        gint d  = MAX(MAX(db, dg), dr);
        if (d > max_diff) { max_diff = d; worst = i; }
    }
    if (max_diff > MAX_PIXEL_DIFF) {
        gint px = (gint)(worst % w), py = (gint)(worst / w);
        g_snprintf(fail_msg, fail_msg_len,
                   "max diff %d at (%d,%d): render BGR=(%d,%d,%d) golden BGR=(%d,%d,%d)",
                   max_diff, px, py,
                   bgr[worst*3+0], bgr[worst*3+1], bgr[worst*3+2],
                   golden_bgr[worst*3+0], golden_bgr[worst*3+1], golden_bgr[worst*3+2]);
    }
    return max_diff;
}
