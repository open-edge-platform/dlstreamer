/*******************************************************************************
 * Copyright (C) 2024-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_ANALYTICS_KEYPOINTS_MTD__
#define __GST_ANALYTICS_KEYPOINTS_MTD__

#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gstanalyticsclassificationmtd.h>
#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/gst.h>

#if _MSC_VER
#define GST_ANALYTICS_META_API GST_API_EXPORT
#endif

G_BEGIN_DECLS

/**
 * GstAnalyticsKeypointMtd:
 * @id: Instance identifier.
 * @meta: Instance of #GstAnalyticsRelationMeta where the analysis-metadata
 * identified by @id is stored.
 *
 * Handle to #GstAnalyticsKeypoint data structure.
 * This type is generally expected to be allocated on the stack.
 *
 * Since: 1.26
 */
typedef GstAnalyticsMtd GstAnalyticsKeypointMtd;

/**
 * GstAnalyticsKeypoint:
 * @x: zero-based absolute x pixel coordinate of a keypoint relative to image upper-left corner.
 * @y: zero-based absolute y pixel coordinate of a keypoint relative to image upper-left corner.
 * @z: normalized depth coordinate of a keypoint, relative to keypoint group center (use 0.0f for 2D keypoints).
 * @v: visibility of a kepoint, normalized <0.0f - not visible, 1.0f - fully visible>.
 */
typedef struct {
    guint x;
    guint y;
    gfloat z;
    gfloat v;
} GstAnalyticsKeypoint;

GST_ANALYTICS_META_API
GstAnalyticsMtdType gst_analytics_keypoint_mtd_get_mtd_type(void);

GST_ANALYTICS_META_API
gboolean gst_analytics_keypoint_mtd_get(const GstAnalyticsKeypointMtd *handle, GstAnalyticsKeypoint *keypoint);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_add_keypoint_mtd(GstAnalyticsRelationMeta *instance,
                                                      const GstAnalyticsKeypoint *keypoint,
                                                      GstAnalyticsKeypointMtd *keypoint_mtd);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_get_keypoint_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                      GstAnalyticsKeypointMtd *rlt);

/**
 * GstAnalyticsKeypointSkeletonMtd:
 * @id: Instance identifier.
 * @meta: Instance of #GstAnalyticsRelationMeta where the analysis-metadata
 * identified by @id is stored.
 *
 * Handle containing data required to use gst_analytics_keypoint_skeleton_mtd APIs.
 * This type is generally expected to be allocated on the stack.
 *
 * Since: 1.26
 */
typedef GstAnalyticsMtd GstAnalyticsKeypointSkeletonMtd;

/**
 * GstAnalyticsKeypointPair:
 * @kp1: index of the first keypoint in a skeleton link.
 * @kp2: index of the second keypoint in a skeleton link.
 *
 * A pair of keypoints linked in a skeleton.
 */
typedef struct {
    guint kp1;
    guint kp2;
} GstAnalyticsKeypointPair;

GST_ANALYTICS_META_API
GstAnalyticsMtdType gst_analytics_keypoint_skeleton_mtd_get_mtd_type(void);

GST_ANALYTICS_META_API
gsize gst_analytics_keypoint_skeleton_mtd_get_count(const GstAnalyticsKeypointSkeletonMtd *handle);

GST_ANALYTICS_META_API
gboolean gst_analytics_keypoint_skeleton_mtd_get(const GstAnalyticsKeypointSkeletonMtd *handle,
                                                 GstAnalyticsKeypointPair *segment, gsize index);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_add_keypoint_skeleton_mtd(GstAnalyticsRelationMeta *instance,
                                                               const gsize skeleton_count,
                                                               const GstAnalyticsKeypointPair *skeletons,
                                                               GstAnalyticsKeypointSkeletonMtd *keypoint_skeleton_mtd);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_get_keypoint_skeleton_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                               GstAnalyticsKeypointSkeletonMtd *rlt);

/**
 * GstAnalyticsKeypointGroupMtd:
 * @id: Instance identifier.
 * @meta: Instance of #GstAnalyticsRelationMeta where the analysis-metadata
 * identified by @id is stored.
 *
 * Handle containing data required to use gst_analytics_keypointgroup_mtd APIs.
 * This type is generally expected to be allocated on the stack.
 *
 * Since: 1.26
 */
typedef GstAnalyticsMtd GstAnalyticsKeypointGroupMtd;

GST_ANALYTICS_META_API
GstAnalyticsMtdType gst_analytics_keypointgroup_mtd_get_mtd_type(void);

GST_ANALYTICS_META_API
GstAnalyticsMtdType gst_analytics_keypoint_group_mtd_get_mtd_type(void);

GST_ANALYTICS_META_API
gsize gst_analytics_keypointgroup_mtd_get_count(const GstAnalyticsKeypointGroupMtd *handle);

GST_ANALYTICS_META_API
gboolean gst_analytics_keypointgroup_mtd_get_keypoint_mtd(const GstAnalyticsKeypointGroupMtd *handle,
                                                          GstAnalyticsKeypointMtd *keypoint_mtd, gsize index);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_add_keypointgroup_mtd(GstAnalyticsRelationMeta *instance,
                                                           const gsize keypoint_count,
                                                           const GstAnalyticsKeypointMtd *keypoints,
                                                           GstAnalyticsKeypointGroupMtd *keypoint_group_mtd);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_get_keypointgroup_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                           GstAnalyticsKeypointGroupMtd *rlt);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_set_keypointgroup_relations(GstAnalyticsRelationMeta *instance,
                                                                 GstAnalyticsKeypointGroupMtd *keypoint_group,
                                                                 GstAnalyticsClsMtd *keypoint_names,
                                                                 GstAnalyticsKeypointSkeletonMtd *keypoint_skeleton);

G_END_DECLS
#endif // __gst_analytics_keypointgroup_MTD__