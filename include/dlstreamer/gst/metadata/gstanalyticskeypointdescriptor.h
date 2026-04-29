/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_ANALYTICS_KEYPOINT_DESCRIPTOR_H__
#define __GST_ANALYTICS_KEYPOINT_DESCRIPTOR_H__

#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsKeypointDescriptor:
 * @semantic_tag: Unique identifier string for this keypoint format.
 * @point_names: Array of keypoint name strings.
 * @point_count: Number of keypoints in @point_names.
 * @skeleton_connections: Array of int pairs (index_a, index_b) into
 *   @point_names describing the skeleton edges. Length is
 *   @skeleton_connection_count * 2.
 * @skeleton_connection_count: Number of skeleton connection pairs.
 *
 * Descriptor that associates a semantic tag with a set of keypoint names
 * and their skeleton connections, so that code consuming keypoint metadata
 * can look up the layout by tag.
 */
typedef struct {
    const char *semantic_tag;
    const char *const *point_names;
    gsize point_count;
    const gint *skeleton_connections;
    gsize skeleton_connection_count;
} GstAnalyticsKeypointDescriptor;

/* Semantic tag string constants */
#define GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17 "body-pose/coco-17"
#define GST_ANALYTICS_KEYPOINT_BODY_POSE_OPENPOSE_18 "body-pose/openpose-18"
#define GST_ANALYTICS_KEYPOINT_BODY_POSE_HRNET_COCO_17 "body-pose/hrnet-coco-17"
#define GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5 "face-landmarks/centerface-5"

GST_ANALYTICS_META_API
const GstAnalyticsKeypointDescriptor *gst_analytics_keypoint_descriptor_lookup(const gchar *semantic_tag);

#ifdef __GI_SCANNER__
/*
 * Indexed accessors for GObject Introspection (Python bindings) only.
 * C/C++ code should access struct fields directly or use
 * descriptor->point_names[index] and descriptor->skeleton_connections[index*2].
 */
GST_ANALYTICS_META_API
const gchar *gst_analytics_keypoint_descriptor_get_point_name(const GstAnalyticsKeypointDescriptor *desc, gsize index);

GST_ANALYTICS_META_API
gboolean gst_analytics_keypoint_descriptor_get_skeleton_connection(const GstAnalyticsKeypointDescriptor *desc,
                                                                   gsize index, gint *from_idx, gint *to_idx);
#endif

G_END_DECLS

#endif /* __GST_ANALYTICS_KEYPOINT_DESCRIPTOR_H__ */
