/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "dlstreamer/gst/metadata/gstgvastreammuxmeta.h"

static gboolean gst_gva_streammux_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
    (void)params;
    (void)buffer;
    GstGvaStreammuxMeta *smeta = (GstGvaStreammuxMeta *)meta;
    smeta->source_id = 0;
    smeta->batch_id = 0;
    smeta->num_sources = 0;
    return TRUE;
}

static gboolean gst_gva_streammux_meta_transform(GstBuffer *dest, GstMeta *meta, GstBuffer *buffer, GQuark type,
                                                  gpointer data) {
    (void)buffer;
    (void)type;
    (void)data;
    GstGvaStreammuxMeta *smeta = (GstGvaStreammuxMeta *)meta;
    gst_buffer_add_gva_streammux_meta(dest, smeta->source_id, smeta->batch_id, smeta->num_sources);
    return TRUE;
}

static void gst_gva_streammux_meta_free(GstMeta *meta, GstBuffer *buffer) {
    (void)meta;
    (void)buffer;
}

GType gst_gva_streammux_meta_api_get_type(void) {
    static GType type = 0;
    static const gchar *tags[] = {NULL};
    if (g_once_init_enter(&type)) {
        GType _type = gst_meta_api_type_register("GstGvaStreammuxMetaAPI", tags);
        g_once_init_leave(&type, _type);
    }
    return type;
}

const GstMetaInfo *gst_gva_streammux_meta_get_info(void) {
    static const GstMetaInfo *meta_info = NULL;
    if (g_once_init_enter(&meta_info)) {
        const GstMetaInfo *mi = gst_meta_register(
            GST_GVA_STREAMMUX_META_API_TYPE, "GstGvaStreammuxMeta", sizeof(GstGvaStreammuxMeta),
            gst_gva_streammux_meta_init, gst_gva_streammux_meta_free, gst_gva_streammux_meta_transform);
        g_once_init_leave(&meta_info, mi);
    }
    return meta_info;
}

GstGvaStreammuxMeta *gst_buffer_add_gva_streammux_meta(GstBuffer *buffer, guint source_id, guint64 batch_id,
                                                       guint num_sources) {
    g_return_val_if_fail(GST_IS_BUFFER(buffer), NULL);
    GstGvaStreammuxMeta *meta =
        (GstGvaStreammuxMeta *)gst_buffer_add_meta(buffer, GST_GVA_STREAMMUX_META_INFO, NULL);
    if (meta) {
        meta->source_id = source_id;
        meta->batch_id = batch_id;
        meta->num_sources = num_sources;
    }
    return meta;
}

GstGvaStreammuxMeta *gst_buffer_get_gva_streammux_meta(GstBuffer *buffer) {
    g_return_val_if_fail(GST_IS_BUFFER(buffer), NULL);
    return (GstGvaStreammuxMeta *)gst_buffer_get_meta(buffer, GST_GVA_STREAMMUX_META_API_TYPE);
}
