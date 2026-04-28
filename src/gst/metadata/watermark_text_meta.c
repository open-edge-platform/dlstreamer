/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <string.h>

#include <gst/video/video.h>

#include "dlstreamer/gst/metadata/watermark_text_meta.h"

#define UNUSED(x) (void)(x)

DLS_EXPORT GType watermark_text_meta_api_get_type(void) {
    static GType type;
    static const gchar *tags[] = {GST_META_TAG_VIDEO_STR, GST_META_TAG_VIDEO_SIZE_STR, NULL};

    if (g_once_init_enter(&type)) {
        GType _type = gst_meta_api_type_register(WATERMARK_TEXT_META_API_NAME, tags);
        g_once_init_leave(&type, _type);
    }
    return type;
}

gboolean watermark_text_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
    UNUSED(params);
    UNUSED(buffer);

    WatermarkTextMeta *text_meta = (WatermarkTextMeta *)meta;
    text_meta->pos.x = 0;
    text_meta->pos.y = 0;
    text_meta->text = NULL;
    text_meta->font_scale = 0.5f;
    text_meta->font_type = WATERMARK_FONT_TRIPLEX;
    text_meta->r = 255;
    text_meta->g = 255;
    text_meta->b = 255;
    text_meta->thickness = 1;
    text_meta->draw_bg = FALSE;
    return TRUE;
}

void watermark_text_meta_free(GstMeta *meta, GstBuffer *buffer) {
    UNUSED(buffer);

    WatermarkTextMeta *text_meta = (WatermarkTextMeta *)meta;
    if (text_meta->text) {
        g_free(text_meta->text);
        text_meta->text = NULL;
    }
}

gboolean watermark_text_meta_transform(GstBuffer *dest_buf, GstMeta *src_meta, GstBuffer *src_buf, GQuark type,
                                       gpointer data) {
    UNUSED(src_buf);

    g_return_val_if_fail(gst_buffer_is_writable(dest_buf), FALSE);

    WatermarkTextMeta *dst = WATERMARK_TEXT_META_ADD(dest_buf);
    WatermarkTextMeta *src = (WatermarkTextMeta *)src_meta;

    if (GST_VIDEO_META_TRANSFORM_IS_SCALE(type)) {
        GstVideoMetaTransform *scale = (GstVideoMetaTransform *)data;
        gdouble scale_x = (gdouble)scale->out_info->width / scale->in_info->width;
        gdouble scale_y = (gdouble)scale->out_info->height / scale->in_info->height;
        dst->pos.x = (guint32)(src->pos.x * scale_x + 0.5);
        dst->pos.y = (guint32)(src->pos.y * scale_y + 0.5);
    } else {
        dst->pos.x = src->pos.x;
        dst->pos.y = src->pos.y;
    }
    dst->font_scale = src->font_scale;
    dst->font_type = src->font_type;
    dst->r = src->r;
    dst->g = src->g;
    dst->b = src->b;
    dst->thickness = src->thickness;
    dst->draw_bg = src->draw_bg;

    if (dst->text) {
        g_free(dst->text);
    }
    dst->text = g_strdup(src->text);

    return TRUE;
}

DLS_EXPORT const GstMetaInfo *watermark_text_meta_get_info(void) {
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter(&meta_info)) {
        const GstMetaInfo *meta = gst_meta_register(
            watermark_text_meta_api_get_type(), WATERMARK_TEXT_META_IMPL_NAME, sizeof(WatermarkTextMeta),
            (GstMetaInitFunction)watermark_text_meta_init, (GstMetaFreeFunction)watermark_text_meta_free,
            (GstMetaTransformFunction)watermark_text_meta_transform);
        g_once_init_leave(&meta_info, meta);
    }
    return meta_info;
}

DLS_EXPORT WatermarkTextMeta *watermark_text_meta_add(GstBuffer *buf, guint32 x, guint32 y, const gchar *text,
                                                      gfloat font_scale, gint font_type, guint8 r, guint8 g, guint8 b,
                                                      gint thickness, gboolean draw_bg) {
    g_return_val_if_fail(buf != NULL, NULL);
    g_return_val_if_fail(text != NULL, NULL);

    WatermarkTextMeta *meta = WATERMARK_TEXT_META_ADD(buf);
    if (!meta)
        return NULL;

    meta->pos.x = x;
    meta->pos.y = y;
    meta->text = g_strdup(text);
    meta->font_scale = font_scale;
    meta->font_type = font_type;
    meta->r = r;
    meta->g = g;
    meta->b = b;
    meta->thickness = thickness;
    meta->draw_bg = draw_bg;
    return meta;
}
