/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <string.h>

#include "dlstreamer/gst/metadata/watermark_text_meta.h"

#define UNUSED(x) (void)(x)

DLS_EXPORT GType watermark_text_meta_api_get_type(void) {
    static GType type;
    static const gchar *tags[] = {NULL};

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
    UNUSED(type);
    UNUSED(data);

    g_return_val_if_fail(gst_buffer_is_writable(dest_buf), FALSE);

    WatermarkTextMeta *dst = WATERMARK_TEXT_META_ADD(dest_buf);
    WatermarkTextMeta *src = (WatermarkTextMeta *)src_meta;

    dst->pos.x = src->pos.x;
    dst->pos.y = src->pos.y;
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
