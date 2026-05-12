/* Video meta matrix transform functions
 * Extracted from upstream GStreamer gstvideometa.c (1.28+)
 * These functions are not available in the system GStreamer version.
 *
 * Copyright (C) <2011> Wim Taymans <wim.taymans@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "dlstreamer/gst/metadata/gstvideometa_matrix.h"
#include <string.h>

G_GNUC_INTERNAL GST_DEBUG_CATEGORY(gst_analytics_relation_meta_debug);
#define GST_CAT_DEFAULT gst_analytics_relation_meta_debug

GQuark gst_video_meta_transform_matrix_get_quark(void) {
    static GQuark _value = 0;

    if (_value == 0) {
        _value = g_quark_from_static_string("gst-video-matrix");
    }
    return _value;
}

void gst_video_meta_transform_matrix_init(GstVideoMetaTransformMatrix *trans, const GstVideoInfo *in_info,
                                          const GstVideoRectangle *in_rectangle, const GstVideoInfo *out_info,
                                          const GstVideoRectangle *out_rectangle) {
    g_return_if_fail(in_info != NULL);
    g_return_if_fail(out_info != NULL);
    g_return_if_fail(in_rectangle->w > 0);
    g_return_if_fail(in_rectangle->h > 0);
    g_return_if_fail(out_rectangle->w > 0);
    g_return_if_fail(out_rectangle->h > 0);

    trans->in_info = in_info;
    trans->out_info = out_info;
    trans->in_rectangle = *in_rectangle;
    trans->out_rectangle = *out_rectangle;

    memset(trans->matrix, 0, sizeof(gfloat) * 9);

    trans->matrix[0][0] = (gfloat)trans->out_rectangle.w / (gfloat)trans->in_rectangle.w;
    trans->matrix[1][1] = (gfloat)trans->out_rectangle.h / (gfloat)trans->in_rectangle.h;
    trans->matrix[2][2] = 1;
}

static gboolean _gst_video_meta_transform_matrix_point(const GstVideoMetaTransformMatrix *transform, gint *x, gint *y,
                                                       gboolean clip) {
    gboolean ret = TRUE;
    gdouble x_in = *x - transform->in_rectangle.x;
    gdouble y_in = *y - transform->in_rectangle.y;
    gdouble x_temp, y_temp;
    gdouble w_prime = transform->matrix[2][0] * x_in + transform->matrix[2][1] * y_in + transform->matrix[2][2];

    if (w_prime == 0.0f) {
        *x = transform->out_rectangle.x;
        *y = transform->out_rectangle.y;
        g_return_val_if_fail(w_prime != 0.0, FALSE);
    }

    if (w_prime == 1.0f) {
        x_temp = transform->matrix[0][0] * x_in + transform->matrix[0][1] * y_in + transform->matrix[0][2];
        y_temp = transform->matrix[1][0] * x_in + transform->matrix[1][1] * y_in + transform->matrix[1][2];
    } else {
        x_temp = (transform->matrix[0][0] * x_in + transform->matrix[0][1] * y_in + transform->matrix[0][2]) / w_prime;
        y_temp = (transform->matrix[1][0] * x_in + transform->matrix[1][1] * y_in + transform->matrix[1][2]) / w_prime;
    }

    *x = x_temp + 0.5f + transform->out_rectangle.x;
    *y = y_temp + 0.5f + transform->out_rectangle.y;

    if (clip) {
        if (*x < transform->out_rectangle.x) {
            *x = transform->out_rectangle.x;
            ret = FALSE;
        }

        if (*x >= transform->out_rectangle.x + transform->out_rectangle.w) {
            *x = transform->out_rectangle.x + transform->out_rectangle.w - 1;
            ret = FALSE;
        }

        if (*y < transform->out_rectangle.y) {
            *y = transform->out_rectangle.y;
            ret = FALSE;
        }

        if (*y >= transform->out_rectangle.y + transform->out_rectangle.h) {
            *y = transform->out_rectangle.y + transform->out_rectangle.h - 1;
            ret = FALSE;
        }
    }

    return ret;
}

gboolean gst_video_meta_transform_matrix_point(const GstVideoMetaTransformMatrix *transform, gint *x, gint *y) {
    return _gst_video_meta_transform_matrix_point(transform, x, y, FALSE);
}

gboolean gst_video_meta_transform_matrix_point_clipped(const GstVideoMetaTransformMatrix *transform, gint *x, gint *y) {
    return _gst_video_meta_transform_matrix_point(transform, x, y, TRUE);
}

static gboolean _gst_video_meta_transform_matrix_rectangle(const GstVideoMetaTransformMatrix *transform,
                                                           GstVideoRectangle *rect, gboolean clip) {
    gboolean ret = TRUE;
    gint x1, y1;
    gint x2, y2;

    if (transform->matrix[2][0] != 0 || transform->matrix[2][1] != 0 || transform->matrix[2][2] != 1)
        return FALSE;

    if ((transform->matrix[0][0] != 0 || transform->matrix[1][1] != 0) &&
        (transform->matrix[0][1] != 0 || transform->matrix[1][0] != 0))
        return FALSE;

    x1 = rect->x;
    y1 = rect->y;
    x2 = rect->x + rect->w;
    y2 = rect->y + rect->h;

    ret = _gst_video_meta_transform_matrix_point(transform, &x1, &y1, clip) &&
          _gst_video_meta_transform_matrix_point(transform, &x2, &y2, clip);

    rect->x = MIN(x1, x2);
    rect->y = MIN(y1, y2);

    rect->w = MAX(x1, x2) - rect->x;
    rect->h = MAX(y1, y2) - rect->y;

    return ret;
}

gboolean gst_video_meta_transform_matrix_rectangle(const GstVideoMetaTransformMatrix *transform,
                                                   GstVideoRectangle *rect) {
    return _gst_video_meta_transform_matrix_rectangle(transform, rect, FALSE);
}

gboolean gst_video_meta_transform_matrix_rectangle_clipped(const GstVideoMetaTransformMatrix *transform,
                                                           GstVideoRectangle *rect) {
    return _gst_video_meta_transform_matrix_rectangle(transform, rect, TRUE);
}
