/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file latency_tracer_meta.h
 * @brief This file contains helper functions to control _LatencyTracerMeta instances
 */

#ifndef __LATENCY_TRACER_META_H__
#define __LATENCY_TRACER_META_H__

#include <gst/gst.h>

#define LATENCY_TRACER_META_API_NAME "LatencyTracerMetaAPI"
#define LATENCY_TRACER_META_IMPL_NAME "LatencyTracerMeta"

G_BEGIN_DECLS

typedef struct _LatencyTracerMeta LatencyTracerMeta;

/**
 * @brief This struct represents Latency Tracer metadata
 */
struct _LatencyTracerMeta {
    GstMeta meta; /**< parent GstMeta */
    GstClockTime init_ts;
    GstClockTime last_pad_push_ts;
};

/**
 * @brief This function registers, if needed, and returns GstMetaInfo for _LatencyTracerMeta
 * @return const GstMetaInfo* for registered type
 */
const GstMetaInfo *latency_tracer_meta_get_info(void);

/**
 * @brief This function registers, if needed, and returns a GType for api "LatencyTracerMetaAPI" and associate it
 * with LATENCY_TRACER_META_TAG tag
 * @return GType type
 */
GType latency_tracer_meta_api_get_type(void);

/**
 * @def LATENCY_TRACER_META_INFO
 * @brief This macro calls latency_tracer_meta_get_info
 * @return const GstMetaInfo* for registered type
 */
#define LATENCY_TRACER_META_INFO (latency_tracer_meta_get_info())

/**
 * @def LATENCY_TRACER_META_GET
 * @brief This macro retrieves ptr to _LatencyTracerMeta instance for passed buf
 * @param buf GstBuffer* of which metadata is retrieved
 * @return _LatencyTracerMeta* instance attached to buf
 */
#define LATENCY_TRACER_META_GET(buf) ((LatencyTracerMeta *)gst_buffer_get_meta(buf, latency_tracer_meta_api_get_type()))

/**
 * @def LATENCY_TRACER_META_ADD
 * @brief This macro attaches new _LatencyTracerMeta instance to passed buf
 * @param buf GstBuffer* to which metadata will be attached
 * @return _LatencyTracerMeta* of the newly added instance attached to buf
 */
#define LATENCY_TRACER_META_ADD(buf)                                                                                   \
    ((LatencyTracerMeta *)gst_buffer_add_meta(buf, latency_tracer_meta_get_info(), NULL))

G_END_DECLS

#endif /* __LATENCY_TRACER_META_H__ */
