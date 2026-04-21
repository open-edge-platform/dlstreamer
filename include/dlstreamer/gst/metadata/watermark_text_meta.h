/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file watermark_text_meta.h
 * @brief Metadata for text drawing on watermark
 */

#ifndef __WATERMARK_TEXT_META_H__
#define __WATERMARK_TEXT_META_H__

#include "gva_export.h"
#include "watermark_common_meta.h"
#include <gst/gst.h>

#define WATERMARK_TEXT_META_API_NAME "WatermarkTextMetaAPI"
#define WATERMARK_TEXT_META_IMPL_NAME "WatermarkTextMeta"

G_BEGIN_DECLS

typedef struct _WatermarkTextMeta WatermarkTextMeta;

/**
 * @brief Font type enumeration
 */
typedef enum {
    WATERMARK_FONT_SIMPLEX = 0,        /**< Hershey simplex (simple) */
    WATERMARK_FONT_PLAIN = 1,          /**< Hershey plain */
    WATERMARK_FONT_DUPLEX = 2,         /**< Hershey duplex */
    WATERMARK_FONT_COMPLEX = 3,        /**< Hershey complex */
    WATERMARK_FONT_TRIPLEX = 4,        /**< Hershey triplex (default) */
    WATERMARK_FONT_COMPLEX_SMALL = 5,  /**< Hershey complex small */
    WATERMARK_FONT_SCRIPT_SIMPLEX = 6, /**< Hershey script simplex */
    WATERMARK_FONT_SCRIPT_COMPLEX = 7  /**< Hershey script complex */
} WatermarkFontType;

/**
 * @brief This struct represents text metadata for watermark
 */
struct _WatermarkTextMeta {
    GstMeta meta;       /**< parent GstMeta */
    WatermarkPoint pos; /**< Text position (top-left) */
    gchar *text;        /**< Text content */
    gfloat font_scale;  /**< Font scale factor (0.1 to 2.0) */
    gint font_type;     /**< Font type (WatermarkFontType) */
    guint8 r;           /**< Red color component */
    guint8 g;           /**< Green color component */
    guint8 b;           /**< Blue color component */
    gint thickness;     /**< Text thickness */
    gboolean draw_bg;   /**< Draw background behind text */
};

/**
 * @brief This function registers and returns GstMetaInfo for WatermarkTextMeta
 * @return const GstMetaInfo* for registered type
 */
DLS_EXPORT const GstMetaInfo *watermark_text_meta_get_info(void);

/**
 * @brief This function registers and returns GType for WatermarkTextMetaAPI
 * @return GType type
 */
DLS_EXPORT GType watermark_text_meta_api_get_type(void);

/**
 * watermark_text_meta_add:
 * @buf: a #GstBuffer to attach metadata to
 * @x: text x position in pixels (top-left)
 * @y: text y position in pixels (top-left)
 * @text: text string to render
 * @font_scale: font scale factor (e.g. 0.7)
 * @font_type: font type as #WatermarkFontType integer
 * @r: red color component
 * @g: green color component
 * @b: blue color component
 * @thickness: text stroke thickness
 * @draw_bg: whether to draw a filled background rectangle behind the text
 *
 * Creates and attaches a new #WatermarkTextMeta to @buf.
 *
 * Returns: (transfer none) (nullable): the newly added #WatermarkTextMeta, or NULL on error
 */
DLS_EXPORT WatermarkTextMeta *watermark_text_meta_add(GstBuffer *buf, guint32 x, guint32 y, const gchar *text,
                                                      gfloat font_scale, gint font_type,
                                                      guint8 r, guint8 g, guint8 b, gint thickness,
                                                      gboolean draw_bg);

/**
 * @def WATERMARK_TEXT_META_INFO
 * @brief This macro calls watermark_text_meta_get_info
 * @return const GstMetaInfo* for registered type
 */
#define WATERMARK_TEXT_META_INFO (watermark_text_meta_get_info())

/**
 * @def WATERMARK_TEXT_META_GET
 * @brief This macro retrieves ptr to WatermarkTextMeta instance for passed buf
 * @param buf GstBuffer* of which metadata is retrieved
 * @return WatermarkTextMeta* instance attached to buf
 */
#define WATERMARK_TEXT_META_GET(buf) ((WatermarkTextMeta *)gst_buffer_get_meta(buf, watermark_text_meta_api_get_type()))

/**
 * @def WATERMARK_TEXT_META_ITERATE
 * @brief This macro iterates through WatermarkTextMeta instances
 * @param buf GstBuffer* of which metadata is iterated
 * @param state gpointer* that updates with opaque pointer
 * @return WatermarkTextMeta* instance attached to buf
 */
#define WATERMARK_TEXT_META_ITERATE(buf, state)                                                                        \
    ((WatermarkTextMeta *)gst_buffer_iterate_meta_filtered(buf, state, watermark_text_meta_api_get_type()))

/**
 * @def WATERMARK_TEXT_META_ADD
 * @brief This macro attaches new WatermarkTextMeta instance to passed buf
 * @param buf GstBuffer* to which metadata will be attached
 * @return WatermarkTextMeta* of the newly added instance
 */
#define WATERMARK_TEXT_META_ADD(buf)                                                                                   \
    ((WatermarkTextMeta *)gst_buffer_add_meta(buf, watermark_text_meta_get_info(), NULL))

G_END_DECLS

#endif /* __WATERMARK_TEXT_META_H__ */
