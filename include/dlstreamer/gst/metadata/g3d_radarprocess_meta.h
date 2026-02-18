/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_RADAR_PROCESS_META_H__
#define __GST_RADAR_PROCESS_META_H__

#include "gva_export.h"
#include <gst/gst.h>

G_BEGIN_DECLS

// Forward declarations for libradar types
typedef struct RadarPointClouds RadarPointClouds;
typedef struct ClusterResult ClusterResult;
typedef struct TrackingResult TrackingResult;

#define GST_RADAR_PROCESS_META_API_TYPE (gst_radar_process_meta_api_get_type())
#define GST_RADAR_PROCESS_META_INFO (gst_radar_process_meta_get_info())

typedef struct _GstRadarProcessMeta GstRadarProcessMeta;

/**
 * GstRadarProcessMeta:
 * @meta: parent #GstMeta
 * @frame_id: Frame ID
 * @point_clouds: Radar point clouds detection results
 * @cluster_result: Clustering results
 * @tracking_result: Tracking results
 *
 * Custom metadata for radar processing results
 */
struct _GstRadarProcessMeta {
    GstMeta meta;

    guint64 frame_id;

    // Point clouds
    gint point_clouds_len;
    gfloat *ranges;
    gfloat *speeds;
    gfloat *angles;
    gfloat *snrs;

    // Cluster result
    gint num_clusters;
    gint *cluster_idx;  // cluster indices
    gfloat *cluster_cx; // center x
    gfloat *cluster_cy; // center y
    gfloat *cluster_rx; // radius x
    gfloat *cluster_ry; // radius y
    gfloat *cluster_av; // average velocity

    // Tracking result
    gint num_tracked_objects;
    gint *tracker_ids;
    gfloat *tracker_x;  // sHat[0]
    gfloat *tracker_y;  // sHat[1]
    gfloat *tracker_vx; // sHat[2]
    gfloat *tracker_vy; // sHat[3]
};

DLS_EXPORT GType gst_radar_process_meta_api_get_type(void);
DLS_EXPORT const GstMetaInfo *gst_radar_process_meta_get_info(void);

DLS_EXPORT GstRadarProcessMeta *gst_buffer_add_radar_process_meta(GstBuffer *buffer, guint64 frame_id,
                                                                  const RadarPointClouds *point_clouds,
                                                                  const ClusterResult *cluster_result,
                                                                  const TrackingResult *tracking_result);

G_END_DECLS

#endif /* __GST_RADAR_PROCESS_META_H__ */
