/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file watermark_circle_meta.h
 * @brief Metadata for circle drawing on watermark
 */

#ifndef __WATERMARK_CIRCLE_META_H__
#define __WATERMARK_CIRCLE_META_H__

#include "gva_export.h"
#include "watermark_common_meta.h"
#include <gst/gst.h>

#define WATERMARK_CIRCLE_META_API_NAME "WatermarkCircleMetaAPI"
#define WATERMARK_CIRCLE_META_IMPL_NAME "WatermarkCircleMeta"

G_BEGIN_DECLS

typedef struct _WatermarkCircleMeta WatermarkCircleMeta;

/**
 * @brief This struct represents circle metadata for watermark
 */
struct _WatermarkCircleMeta {
    GstMeta meta;          /**< parent GstMeta */
    WatermarkPoint center; /**< Circle center point */
    guint32 radius;        /**< Circle radius in pixels */
    guint8 r;              /**< Red color component */
    guint8 g;              /**< Green color component */
    guint8 b;              /**< Blue color component */
    gint thickness;        /**< Line thickness (for outline, -1 for filled) */
};

/**
 * @brief This function registers and returns GstMetaInfo for WatermarkCircleMeta
 * @return const GstMetaInfo* for registered type
 */
DLS_EXPORT const GstMetaInfo *watermark_circle_meta_get_info(void);

/**
 * @brief This function registers and returns GType for WatermarkCircleMetaAPI
 * @return GType type
 */
DLS_EXPORT GType watermark_circle_meta_api_get_type(void);

/**
 * watermark_circle_meta_add:
 * @buf: a #GstBuffer to attach metadata to
 * @cx: center x coordinate in pixels
 * @cy: center y coordinate in pixels
 * @radius: circle radius in pixels
 * @r: red color component
 * @g: green color component
 * @b: blue color component
 * @thickness: line thickness (-1 for filled circle)
 *
 * Creates and attaches a new #WatermarkCircleMeta to @buf.
 *
 * Returns: (transfer none) (nullable): the newly added #WatermarkCircleMeta, or NULL on error
 */
DLS_EXPORT WatermarkCircleMeta *watermark_circle_meta_add(GstBuffer *buf, guint32 cx, guint32 cy, guint32 radius,
                                                          guint8 r, guint8 g, guint8 b, gint thickness);

/**
 * @def WATERMARK_CIRCLE_META_INFO
 * @brief This macro calls watermark_circle_meta_get_info
 * @return const GstMetaInfo* for registered type
 */
#define WATERMARK_CIRCLE_META_INFO (watermark_circle_meta_get_info())

/**
 * @def WATERMARK_CIRCLE_META_GET
 * @brief This macro retrieves ptr to WatermarkCircleMeta instance for passed buf
 * @param buf GstBuffer* of which metadata is retrieved
 * @return WatermarkCircleMeta* instance attached to buf
 */
#define WATERMARK_CIRCLE_META_GET(buf)                                                                                 \
    ((WatermarkCircleMeta *)gst_buffer_get_meta(buf, watermark_circle_meta_api_get_type()))

/**
 * @def WATERMARK_CIRCLE_META_ITERATE
 * @brief This macro iterates through WatermarkCircleMeta instances
 * @param buf GstBuffer* of which metadata is iterated
 * @param state gpointer* that updates with opaque pointer
 * @return WatermarkCircleMeta* instance attached to buf
 */
#define WATERMARK_CIRCLE_META_ITERATE(buf, state)                                                                      \
    ((WatermarkCircleMeta *)gst_buffer_iterate_meta_filtered(buf, state, watermark_circle_meta_api_get_type()))

/**
 * @def WATERMARK_CIRCLE_META_ADD
 * @brief This macro attaches new WatermarkCircleMeta instance to passed buf
 * @param buf GstBuffer* to which metadata will be attached
 * @return WatermarkCircleMeta* of the newly added instance
 */
#define WATERMARK_CIRCLE_META_ADD(buf)                                                                                 \
    ((WatermarkCircleMeta *)gst_buffer_add_meta(buf, watermark_circle_meta_get_info(), NULL))

G_END_DECLS

#endif /* __WATERMARK_CIRCLE_META_H__ */
