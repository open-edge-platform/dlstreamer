/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <string.h>

#include <gst/video/video.h>

#include "dlstreamer/gst/metadata/watermark_circle_meta.h"

#define UNUSED(x) (void)(x)

DLS_EXPORT GType watermark_circle_meta_api_get_type(void) {
    static GType type;
    static const gchar *tags[] = {GST_META_TAG_VIDEO_STR, GST_META_TAG_VIDEO_SIZE_STR, NULL};

    if (g_once_init_enter(&type)) {
        GType _type = gst_meta_api_type_register(WATERMARK_CIRCLE_META_API_NAME, tags);
        g_once_init_leave(&type, _type);
    }
    return type;
}

gboolean watermark_circle_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
    UNUSED(params);
    UNUSED(buffer);

    WatermarkCircleMeta *circle_meta = (WatermarkCircleMeta *)meta;
    circle_meta->center.x = 0;
    circle_meta->center.y = 0;
    circle_meta->radius = 0;
    circle_meta->r = 255;
    circle_meta->g = 255;
    circle_meta->b = 255;
    circle_meta->thickness = 2;
    return TRUE;
}

void watermark_circle_meta_free(GstMeta *meta, GstBuffer *buffer) {
    UNUSED(meta);
    UNUSED(buffer);
}

gboolean watermark_circle_meta_transform(GstBuffer *dest_buf, GstMeta *src_meta, GstBuffer *src_buf, GQuark type,
                                         gpointer data) {
    UNUSED(src_buf);

    g_return_val_if_fail(gst_buffer_is_writable(dest_buf), FALSE);

    WatermarkCircleMeta *dst = WATERMARK_CIRCLE_META_ADD(dest_buf);
    WatermarkCircleMeta *src = (WatermarkCircleMeta *)src_meta;

    gdouble scale_x = 1.0, scale_y = 1.0;
    if (GST_VIDEO_META_TRANSFORM_IS_SCALE(type)) {
        GstVideoMetaTransform *scale = (GstVideoMetaTransform *)data;
        scale_x = (gdouble)scale->out_info->width / scale->in_info->width;
        scale_y = (gdouble)scale->out_info->height / scale->in_info->height;
    }

    dst->center.x = (guint32)(src->center.x * scale_x + 0.5);
    dst->center.y = (guint32)(src->center.y * scale_y + 0.5);
    dst->radius = (guint32)(src->radius * (scale_x + scale_y) * 0.5 + 0.5);
    dst->r = src->r;
    dst->g = src->g;
    dst->b = src->b;
    dst->thickness = src->thickness;

    return TRUE;
}

DLS_EXPORT const GstMetaInfo *watermark_circle_meta_get_info(void) {
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter(&meta_info)) {
        const GstMetaInfo *meta = gst_meta_register(
            watermark_circle_meta_api_get_type(), WATERMARK_CIRCLE_META_IMPL_NAME, sizeof(WatermarkCircleMeta),
            (GstMetaInitFunction)watermark_circle_meta_init, (GstMetaFreeFunction)watermark_circle_meta_free,
            (GstMetaTransformFunction)watermark_circle_meta_transform);
        g_once_init_leave(&meta_info, meta);
    }
    return meta_info;
}

DLS_EXPORT WatermarkCircleMeta *watermark_circle_meta_add(GstBuffer *buf, guint32 cx, guint32 cy, guint32 radius,
                                                          guint8 r, guint8 g, guint8 b, gint thickness) {
    g_return_val_if_fail(buf != NULL, NULL);

    WatermarkCircleMeta *meta = WATERMARK_CIRCLE_META_ADD(buf);
    if (!meta)
        return NULL;

    meta->center.x = cx;
    meta->center.y = cy;
    meta->radius = radius;
    meta->r = r;
    meta->g = g;
    meta->b = b;
    meta->thickness = thickness;
    return meta;
}
