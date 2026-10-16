/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <string.h>

#include "dlstreamer/gst/metadata/gva_json_meta.h"

#define UNUSED(x) (void)(x)

DLS_EXPORT GType gst_gva_json_meta_api_get_type(void) {
    static GType type;
    static const gchar *tags[] = {NULL};

    if (g_once_init_enter(&type)) {
        GType _type = gst_meta_api_type_register(GVA_JSON_META_API_NAME, tags);
        g_once_init_leave(&type, _type);
    }
    return type;
}

gboolean gst_gva_json_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
    UNUSED(params);
    UNUSED(buffer);

    GstGVAJSONMeta *json_meta = (GstGVAJSONMeta *)meta;
    json_meta->message = 0;
    return TRUE;
}

gboolean gst_gva_json_meta_transform(GstBuffer *dest_buf, GstMeta *src_meta, GstBuffer *src_buf, GQuark type,
                                     gpointer data) {
    UNUSED(src_buf);
    UNUSED(type);
    UNUSED(data);

    g_return_val_if_fail(gst_buffer_is_writable(dest_buf), FALSE);

    GstGVAJSONMeta *dst = GST_GVA_JSON_META_ADD(dest_buf);
    GstGVAJSONMeta *src = (GstGVAJSONMeta *)src_meta;

    if (dst->message)
        g_free(dst->message);
    dst->message = g_strdup(src->message);
    return TRUE;
}

void gst_gva_json_meta_free(GstMeta *meta, GstBuffer *buffer) {
    UNUSED(buffer);

    GstGVAJSONMeta *json_meta = (GstGVAJSONMeta *)meta;
    if (json_meta->message) {
        g_free(json_meta->message);
        json_meta->message = NULL;
    }
}

DLS_EXPORT const GstMetaInfo *gst_gva_json_meta_get_info(void) {
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter(&meta_info)) {
        const GstMetaInfo *meta =
            gst_meta_register(gst_gva_json_meta_api_get_type(), GVA_JSON_META_IMPL_NAME, sizeof(GstGVAJSONMeta),
                              (GstMetaInitFunction)gst_gva_json_meta_init, (GstMetaFreeFunction)gst_gva_json_meta_free,
                              (GstMetaTransformFunction)gst_gva_json_meta_transform);
        g_once_init_leave(&meta_info, meta);
    }
    return meta_info;
}

DLS_EXPORT gchar *get_json_message(GstGVAJSONMeta *meta) {
    return meta->message;
}

DLS_EXPORT void set_json_message(GstGVAJSONMeta *meta, const gchar *message) {
    gst_gva_json_meta_free((GstMeta *)meta, NULL);
    meta->message = g_strdup(message);
}
