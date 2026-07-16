/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "g3d_test_golden.h"
#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

void
save_png(const gchar *path, const guint8 *bgr, gint w, gint h)
{
    gsize   n   = (gsize)w * h * 3;
    guint8 *rgb = (guint8 *)g_malloc(n);
    for (gsize i = 0; i < (gsize)w * h; i++) {
        rgb[i*3+0] = bgr[i*3+2];
        rgb[i*3+1] = bgr[i*3+1];
        rgb[i*3+2] = bgr[i*3+0];
    }
    if (!stbi_write_png(path, w, h, 3, rgb, w * 3))
        g_warning("save_png: failed to write %s", path);
    else
        g_print("  saved -> %s\n", path);
    g_free(rgb);
}

guint8 *
load_png(const gchar *path, gint *w_out, gint *h_out)
{
    int w, h, ch;
    stbi_uc *raw = stbi_load(path, &w, &h, &ch, 3);
    if (!raw)
        return NULL;
    gsize   n    = (gsize)w * h * 3;
    guint8 *data = (guint8 *)g_memdup2(raw, n);
    stbi_image_free(raw);
    *w_out = w;
    *h_out = h;
    return data;
}

gint
compare_with_golden(const guint8 *bgr,       gint w,  gint h,
                    const guint8 *golden_rgb, gint gw, gint gh,
                    gchar        *fail_msg,   gsize fail_msg_len)
{
    if (w != gw || h != gh) {
        g_snprintf(fail_msg, fail_msg_len,
                   "size mismatch: render=%dx%d golden=%dx%d", w, h, gw, gh);
        return -1;
    }
    gint  max_diff = 0;
    gsize worst    = 0;
    for (gsize i = 0; i < (gsize)w * h; i++) {
        gint dr = abs((gint)bgr[i*3+2] - (gint)golden_rgb[i*3+0]);
        gint dg = abs((gint)bgr[i*3+1] - (gint)golden_rgb[i*3+1]);
        gint db = abs((gint)bgr[i*3+0] - (gint)golden_rgb[i*3+2]);
        gint d  = MAX(MAX(dr, dg), db);
        if (d > max_diff) { max_diff = d; worst = i; }
    }
    if (max_diff > MAX_PIXEL_DIFF) {
        gint px = (gint)(worst % w), py = (gint)(worst / w);
        g_snprintf(fail_msg, fail_msg_len,
                   "max diff %d at (%d,%d): render BGR=(%d,%d,%d) golden RGB=(%d,%d,%d)",
                   max_diff, px, py,
                   bgr[worst*3+2], bgr[worst*3+1], bgr[worst*3+0],
                   golden_rgb[worst*3+0], golden_rgb[worst*3+1], golden_rgb[worst*3+2]);
    }
    return max_diff;
}
