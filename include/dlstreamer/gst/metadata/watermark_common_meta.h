/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file watermark_common_meta.h
 * @brief Common structures for watermark metadata
 */

#ifndef __WATERMARK_COMMON_META_H__
#define __WATERMARK_COMMON_META_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * @brief Point structure for watermark drawing
 */
typedef struct {
    guint32 x; /**< X coordinate (pixel) */
    guint32 y; /**< Y coordinate (pixel) */
} WatermarkPoint;

G_END_DECLS

#endif /* __WATERMARK_COMMON_META_H__ */
