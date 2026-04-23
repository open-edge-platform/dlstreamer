/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <string.h>

#include "dlstreamer/gst/metadata/watermark_draw_meta.h"

#define UNUSED(x) (void)(x)

DLS_EXPORT GType watermark_draw_meta_api_get_type(void) {
    static GType type;
    static const gchar *tags[] = {NULL};

    if (g_once_init_enter(&type)) {
        GType _type = gst_meta_api_type_register(WATERMARK_DRAW_META_API_NAME, tags);
        g_once_init_leave(&type, _type);
    }
    return type;
}

gboolean watermark_draw_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
    UNUSED(params);
    UNUSED(buffer);

    WatermarkDrawMeta *draw_meta = (WatermarkDrawMeta *)meta;
    draw_meta->points = NULL;
    draw_meta->point_count = 0;
    draw_meta->r = 255;
    draw_meta->g = 255;
    draw_meta->b = 255;
    draw_meta->thickness = 2;
    return TRUE;
}

void watermark_draw_meta_free(GstMeta *meta, GstBuffer *buffer) {
    UNUSED(buffer);

    WatermarkDrawMeta *draw_meta = (WatermarkDrawMeta *)meta;
    if (draw_meta->points) {
        g_free(draw_meta->points);
        draw_meta->points = NULL;
    }
    draw_meta->point_count = 0;
}

gboolean watermark_draw_meta_transform(GstBuffer *dest_buf, GstMeta *src_meta, GstBuffer *src_buf, GQuark type,
                                       gpointer data) {
    UNUSED(src_buf);
    UNUSED(type);
    UNUSED(data);

    g_return_val_if_fail(gst_buffer_is_writable(dest_buf), FALSE);

    WatermarkDrawMeta *dst = WATERMARK_DRAW_META_ADD(dest_buf);
    WatermarkDrawMeta *src = (WatermarkDrawMeta *)src_meta;

    if (src->point_count > 0 && src->point_count <= WATERMARK_DRAW_MAX_POINTS) {
        dst->points = g_malloc(sizeof(WatermarkPoint) * src->point_count);
        memcpy(dst->points, src->points, sizeof(WatermarkPoint) * src->point_count);
        dst->point_count = src->point_count;
    } else {
        dst->points = NULL;
        dst->point_count = 0;
    }

    dst->r = src->r;
    dst->g = src->g;
    dst->b = src->b;
    dst->thickness = src->thickness;

    return TRUE;
}

DLS_EXPORT const GstMetaInfo *watermark_draw_meta_get_info(void) {
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter(&meta_info)) {
        const GstMetaInfo *meta = gst_meta_register(
            watermark_draw_meta_api_get_type(), WATERMARK_DRAW_META_IMPL_NAME, sizeof(WatermarkDrawMeta),
            (GstMetaInitFunction)watermark_draw_meta_init, (GstMetaFreeFunction)watermark_draw_meta_free,
            (GstMetaTransformFunction)watermark_draw_meta_transform);
        g_once_init_leave(&meta_info, meta);
    }
    return meta_info;
}

// Helper function to add WatermarkDrawMeta to buffer with given parameters
// coords is a flat array of interleaved x,y coordinates: [x1,y1,x2,y2,...]
// n_coords is the number of coordinate values (must be even, n_points = n_coords/2)
// max supported points is WATERMARK_DRAW_MAX_POINTS - 128 so n_coords must be <= 256
// r,g,b are color components, thickness is line thickness
DLS_EXPORT WatermarkDrawMeta *watermark_draw_meta_add(GstBuffer *buf, const guint32 *coords, guint n_coords, guint8 r,
                                                      guint8 g, guint8 b, gint thickness) {
    g_return_val_if_fail(buf != NULL, NULL);
    g_return_val_if_fail(coords != NULL, NULL);
    g_return_val_if_fail(n_coords >= 4 && n_coords % 2 == 0, NULL);

    guint n_points = n_coords / 2;
    g_return_val_if_fail(n_points <= WATERMARK_DRAW_MAX_POINTS, NULL);

    WatermarkDrawMeta *meta = WATERMARK_DRAW_META_ADD(buf);
    if (!meta)
        return NULL;

    meta->points = g_malloc(sizeof(WatermarkPoint) * n_points);
    for (guint i = 0; i < n_points; i++) {
        meta->points[i].x = coords[i * 2];
        meta->points[i].y = coords[i * 2 + 1];
    }
    meta->point_count = n_points;
    meta->r = r;
    meta->g = g;
    meta->b = b;
    meta->thickness = thickness;
    return meta;
}
