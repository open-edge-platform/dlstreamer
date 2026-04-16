/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file watermark_draw_meta.h
 * @brief Metadata for line and polygon drawing on watermark
 */

#ifndef __WATERMARK_DRAW_META_H__
#define __WATERMARK_DRAW_META_H__

#include "gva_export.h"
#include "watermark_common_meta.h"
#include <gst/gst.h>
#include <stddef.h>

#define WATERMARK_DRAW_META_API_NAME "WatermarkDrawMetaAPI"
#define WATERMARK_DRAW_META_IMPL_NAME "WatermarkDrawMeta"

#define WATERMARK_DRAW_MAX_POINTS 128

G_BEGIN_DECLS

typedef struct _WatermarkDrawMeta WatermarkDrawMeta;

/**
 * @brief This struct represents line/polygon metadata for watermark
 * For line: set point_count = 2
 * For polygon: set point_count >= 3
 */
struct _WatermarkDrawMeta {
    GstMeta meta;           /**< parent GstMeta */
    WatermarkPoint *points; /**< Pointer to dynamically allocated array of points */
    guint point_count;      /**< Number of points (2 for line, N for polygon) */
    guint8 r;               /**< Red color component */
    guint8 g;               /**< Green color component */
    guint8 b;               /**< Blue color component */
    gint thickness;         /**< Line thickness */
};

/**
 * @brief This function registers and returns GstMetaInfo for WatermarkDrawMeta
 * @return const GstMetaInfo* for registered type
 */
DLS_EXPORT const GstMetaInfo *watermark_draw_meta_get_info(void);

/**
 * @brief This function registers and returns GType for WatermarkDrawMetaAPI
 * @return GType type
 */
DLS_EXPORT GType watermark_draw_meta_api_get_type(void);

/**
 * @def WATERMARK_DRAW_META_INFO
 * @brief This macro calls watermark_draw_meta_get_info
 * @return const GstMetaInfo* for registered type
 */
#define WATERMARK_DRAW_META_INFO (watermark_draw_meta_get_info())

/**
 * @def WATERMARK_DRAW_META_GET
 * @brief This macro retrieves ptr to WatermarkDrawMeta instance for passed buf
 * @param buf GstBuffer* of which metadata is retrieved
 * @return WatermarkDrawMeta* instance attached to buf
 */
#define WATERMARK_DRAW_META_GET(buf) ((WatermarkDrawMeta *)gst_buffer_get_meta(buf, watermark_draw_meta_api_get_type()))

/**
 * @def WATERMARK_DRAW_META_ITERATE
 * @brief This macro iterates through WatermarkDrawMeta instances
 * @param buf GstBuffer* of which metadata is iterated
 * @param state gpointer* that updates with opaque pointer
 * @return WatermarkDrawMeta* instance attached to buf
 */
#define WATERMARK_DRAW_META_ITERATE(buf, state)                                                                        \
    ((WatermarkDrawMeta *)gst_buffer_iterate_meta_filtered(buf, state, watermark_draw_meta_api_get_type()))

/**
 * @def WATERMARK_DRAW_META_ADD
 * @brief This macro attaches new WatermarkDrawMeta instance to passed buf
 * @param buf GstBuffer* to which metadata will be attached
 * @return WatermarkDrawMeta* of the newly added instance
 */
#define WATERMARK_DRAW_META_ADD(buf)                                                                                   \
    ((WatermarkDrawMeta *)gst_buffer_add_meta(buf, watermark_draw_meta_get_info(), NULL))

G_END_DECLS

#endif /* __WATERMARK_DRAW_META_H__ */
