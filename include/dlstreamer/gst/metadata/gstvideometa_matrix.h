/* GStreamer video meta matrix transform
 * Extracted from upstream GStreamer gstvideometa.h (1.28+)
 * These types and functions are not available in the system GStreamer version.
 *
 * Copyright (C) <2011> Wim Taymans <wim.taymans@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_VIDEO_META_MATRIX_H__
#define __GST_VIDEO_META_MATRIX_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

GQuark gst_video_meta_transform_matrix_get_quark(void);

/**
 * GST_VIDEO_META_TRANSFORM_IS_MATRIX:
 *
 * Checks if a #GType of a meta transformation is a matrix transformation
 *
 * Returns: TRUE if its a matrix transformation
 *
 * Since: 1.28
 */
#define GST_VIDEO_META_TRANSFORM_IS_MATRIX(type) ((type) == gst_video_meta_transform_matrix_get_quark())

/**
 * GstVideoMetaTransformMatrix:
 * @in_info: the input #GstVideoInfo
 * @in_rectangle: the input #GstVideoRectangle
 * @out_info: the output #GstVideoInfo
 * @out_rectangle: the output #GstVideoRectangle
 * @matrix: a 3x3 matrix representing an homographic transformation
 *
 * Extra data passed to a video transform #GstMetaTransformFunction such as:
 * "gst-video-matrix".
 *
 * The matrix represents a transformation that is applied to the content of
 * @in_rectangle, and its output is put inside @out_rectangle. The coordinate
 * system has it's (0, 0) in the top-left corner of the rectangles and
 * goes down and right from there..
 *
 * It's a programming error to have a singular matrix.
 *
 * Since: 1.28
 */
typedef struct {
    const GstVideoInfo *in_info;
    GstVideoRectangle in_rectangle;

    const GstVideoInfo *out_info;
    GstVideoRectangle out_rectangle;

    gfloat matrix[3][3];
} GstVideoMetaTransformMatrix;

void gst_video_meta_transform_matrix_init(GstVideoMetaTransformMatrix *trans, const GstVideoInfo *in_info,
                                          const GstVideoRectangle *in_rectangle, const GstVideoInfo *out_info,
                                          const GstVideoRectangle *out_rectangle);

gboolean gst_video_meta_transform_matrix_point(const GstVideoMetaTransformMatrix *transform, gint *x, gint *y);

gboolean gst_video_meta_transform_matrix_point_clipped(const GstVideoMetaTransformMatrix *transform, gint *x, gint *y);

gboolean gst_video_meta_transform_matrix_rectangle(const GstVideoMetaTransformMatrix *transform,
                                                   GstVideoRectangle *rect);

gboolean gst_video_meta_transform_matrix_rectangle_clipped(const GstVideoMetaTransformMatrix *transform,
                                                           GstVideoRectangle *rect);

G_END_DECLS

#endif /* __GST_VIDEO_META_MATRIX_H__ */
