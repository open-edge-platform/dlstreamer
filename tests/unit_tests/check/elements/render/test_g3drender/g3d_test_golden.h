/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once
#include <gst/gst.h>

#define MAX_PIXEL_DIFF 1

/* Save @bgr (packed, no stride) as a PNG at @path. */
void save_png(const gchar *path, const guint8 *bgr, gint w, gint h);

/*
 * Load a PNG from @path and return a packed RGB buffer.
 * Caller owns the returned buffer; free with g_free().
 * Returns NULL if the file cannot be read.
 */
guint8 *load_png(const gchar *path, gint *w_out, gint *h_out);

/*
 * Compare a rendered frame (packed BGR) against a golden reference (packed RGB).
 * Returns the maximum per-channel absolute difference, or -1 on dimension mismatch.
 * On failure a human-readable description is written to @fail_msg.
 */
gint compare_with_golden(const guint8 *bgr,       gint w,  gint h,
                         const guint8 *golden_rgb, gint gw, gint gh,
                         gchar        *fail_msg,   gsize fail_msg_len);
