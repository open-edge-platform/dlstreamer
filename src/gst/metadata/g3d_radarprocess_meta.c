/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "dlstreamer/gst/metadata/g3d_radarprocess_meta.h"
#include <dlstreamer/radar/libradar.h>
#include <string.h>

DLS_EXPORT GType gst_radar_process_meta_api_get_type(void) {
    static GType type = 0;
    static const gchar *tags[] = {NULL};

    if (g_once_init_enter(&type)) {
        GType _type = gst_meta_api_type_register("GstRadarProcessMetaAPI", tags);
        g_once_init_leave(&type, _type);
    }
    return type;
}

static gboolean gst_radar_process_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
    GstRadarProcessMeta *radar_meta = (GstRadarProcessMeta *)meta;
    (void)params;
    (void)buffer;

    radar_meta->frame_id = 0;
    radar_meta->point_clouds_len = 0;
    radar_meta->ranges = NULL;
    radar_meta->speeds = NULL;
    radar_meta->angles = NULL;
    radar_meta->snrs = NULL;

    radar_meta->num_clusters = 0;
    radar_meta->cluster_idx = NULL;
    radar_meta->cluster_cx = NULL;
    radar_meta->cluster_cy = NULL;
    radar_meta->cluster_rx = NULL;
    radar_meta->cluster_ry = NULL;
    radar_meta->cluster_av = NULL;

    radar_meta->num_tracked_objects = 0;
    radar_meta->tracker_ids = NULL;
    radar_meta->tracker_x = NULL;
    radar_meta->tracker_y = NULL;
    radar_meta->tracker_vx = NULL;
    radar_meta->tracker_vy = NULL;

    return TRUE;
}

static void gst_radar_process_meta_free(GstMeta *meta, GstBuffer *buffer) {
    GstRadarProcessMeta *radar_meta = (GstRadarProcessMeta *)meta;
    (void)buffer;

    // Free point clouds arrays
    g_free(radar_meta->ranges);
    g_free(radar_meta->speeds);
    g_free(radar_meta->angles);
    g_free(radar_meta->snrs);

    // Free cluster arrays
    g_free(radar_meta->cluster_idx);
    g_free(radar_meta->cluster_cx);
    g_free(radar_meta->cluster_cy);
    g_free(radar_meta->cluster_rx);
    g_free(radar_meta->cluster_ry);
    g_free(radar_meta->cluster_av);

    // Free tracking arrays
    g_free(radar_meta->tracker_ids);
    g_free(radar_meta->tracker_x);
    g_free(radar_meta->tracker_y);
    g_free(radar_meta->tracker_vx);
    g_free(radar_meta->tracker_vy);
}

static gboolean gst_radar_process_meta_transform(GstBuffer *transbuf, GstMeta *meta, GstBuffer *buffer, GQuark type,
                                                 gpointer data) {
    (void)transbuf;
    (void)meta;
    (void)buffer;
    (void)type;
    (void)data;

    // For now, we don't transform the metadata
    // Could be implemented to copy metadata to transformed buffer if needed
    return FALSE;
}

DLS_EXPORT const GstMetaInfo *gst_radar_process_meta_get_info(void) {
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter(&meta_info)) {
        const GstMetaInfo *mi = gst_meta_register(GST_RADAR_PROCESS_META_API_TYPE, "GstRadarProcessMeta",
                                                  sizeof(GstRadarProcessMeta), gst_radar_process_meta_init,
                                                  gst_radar_process_meta_free, gst_radar_process_meta_transform);
        g_once_init_leave(&meta_info, mi);
    }
    return meta_info;
}

DLS_EXPORT GstRadarProcessMeta *gst_buffer_add_radar_process_meta(GstBuffer *buffer, guint64 frame_id,
                                                                  const RadarPointClouds *point_clouds,
                                                                  const ClusterResult *cluster_result,
                                                                  const TrackingResult *tracking_result) {
    GstRadarProcessMeta *meta;

    g_return_val_if_fail(GST_IS_BUFFER(buffer), NULL);

    meta = (GstRadarProcessMeta *)gst_buffer_add_meta(buffer, GST_RADAR_PROCESS_META_INFO, NULL);
    if (!meta)
        return NULL;

    meta->frame_id = frame_id;

    // Copy point clouds data
    if (point_clouds && point_clouds->len > 0) {
        meta->point_clouds_len = point_clouds->len;

        meta->ranges = g_new(gfloat, point_clouds->len);
        meta->speeds = g_new(gfloat, point_clouds->len);
        meta->angles = g_new(gfloat, point_clouds->len);
        meta->snrs = g_new(gfloat, point_clouds->len);

        if (point_clouds->range)
            memcpy(meta->ranges, point_clouds->range, point_clouds->len * sizeof(gfloat));
        if (point_clouds->speed)
            memcpy(meta->speeds, point_clouds->speed, point_clouds->len * sizeof(gfloat));
        if (point_clouds->angle)
            memcpy(meta->angles, point_clouds->angle, point_clouds->len * sizeof(gfloat));
        if (point_clouds->snr)
            memcpy(meta->snrs, point_clouds->snr, point_clouds->len * sizeof(gfloat));
    } else {
        meta->point_clouds_len = 0;
    }

    // Copy cluster data
    if (cluster_result && cluster_result->n > 0 && cluster_result->cd) {
        meta->num_clusters = cluster_result->n;

        meta->cluster_idx = g_new(gint, cluster_result->n);
        meta->cluster_cx = g_new(gfloat, cluster_result->n);
        meta->cluster_cy = g_new(gfloat, cluster_result->n);
        meta->cluster_rx = g_new(gfloat, cluster_result->n);
        meta->cluster_ry = g_new(gfloat, cluster_result->n);
        meta->cluster_av = g_new(gfloat, cluster_result->n);

        for (gint i = 0; i < cluster_result->n; i++) {
            meta->cluster_idx[i] = cluster_result->idx ? cluster_result->idx[i] : i;
            meta->cluster_cx[i] = cluster_result->cd[i].cx;
            meta->cluster_cy[i] = cluster_result->cd[i].cy;
            meta->cluster_rx[i] = cluster_result->cd[i].rx;
            meta->cluster_ry[i] = cluster_result->cd[i].ry;
            meta->cluster_av[i] = cluster_result->cd[i].av;
        }
    } else {
        meta->num_clusters = 0;
    }

    // Copy tracking data
    if (tracking_result && tracking_result->len > 0 && tracking_result->td) {
        meta->num_tracked_objects = tracking_result->len;

        meta->tracker_ids = g_new(gint, tracking_result->len);
        meta->tracker_x = g_new(gfloat, tracking_result->len);
        meta->tracker_y = g_new(gfloat, tracking_result->len);
        meta->tracker_vx = g_new(gfloat, tracking_result->len);
        meta->tracker_vy = g_new(gfloat, tracking_result->len);

        for (gint i = 0; i < tracking_result->len; i++) {
            meta->tracker_ids[i] = tracking_result->td[i].tid;
            meta->tracker_x[i] = tracking_result->td[i].sHat[0];
            meta->tracker_y[i] = tracking_result->td[i].sHat[1];
            meta->tracker_vx[i] = tracking_result->td[i].sHat[2];
            meta->tracker_vy[i] = tracking_result->td[i].sHat[3];
        }
    } else {
        meta->num_tracked_objects = 0;
    }

    return meta;
}
