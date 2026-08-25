/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "latency_tracer_meta.h"

#define UNUSED(x) (void)(x)

GType latency_tracer_meta_api_get_type(void) {
    static const gchar *tags[] = {NULL};
    static GType type = gst_meta_api_type_register(LATENCY_TRACER_META_API_NAME, tags);
    return type;
}

gboolean latency_tracer_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
    UNUSED(params);
    UNUSED(buffer);

    LatencyTracerMeta *tracer_meta = (LatencyTracerMeta *)meta;
    tracer_meta->init_ts = 0;
    tracer_meta->last_pad_push_ts = 0;
    return TRUE;
}

gboolean latency_tracer_meta_transform(GstBuffer *dest_buf, GstMeta *src_meta, GstBuffer *src_buf, GQuark type,
                                       gpointer data) {
    UNUSED(src_buf);
    UNUSED(type);
    UNUSED(data);

    g_return_val_if_fail(gst_buffer_is_writable(dest_buf), FALSE);
    LatencyTracerMeta *dst = LATENCY_TRACER_META_ADD(dest_buf);
    LatencyTracerMeta *src = (LatencyTracerMeta *)src_meta;
    dst->init_ts = src->init_ts;
    dst->last_pad_push_ts = src->last_pad_push_ts;
    return TRUE;
}

const GstMetaInfo *latency_tracer_meta_get_info(void) {
    static const GstMetaInfo *meta_info = gst_meta_register(
        latency_tracer_meta_api_get_type(), LATENCY_TRACER_META_IMPL_NAME, sizeof(LatencyTracerMeta),
        (GstMetaInitFunction)latency_tracer_meta_init, NULL, (GstMetaTransformFunction)latency_tracer_meta_transform);
    return meta_info;
}
