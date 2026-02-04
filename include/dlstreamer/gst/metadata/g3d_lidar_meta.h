/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef G3D_LIDARMETA_H
#define G3D_LIDARMETA_H

#include "gva_export.h"
#include <cstddef>
#include <gst/gst.h>
#include <vector>

G_BEGIN_DECLS

typedef struct _LidarMeta {
    GstMeta meta;
    guint lidar_point_count; // Number of points in this frame. Each point occupies 4 floats (x, y, z, intensity) in
                             // lidar_data.
    std::vector<float> lidar_data; // Point data stored as a flat array of floats: [x, y, z, intensity] repeated
                                   // lidar_point_count times.
    size_t frame_id;               // Sequential frame identifier from the source stream.
    GstClockTime exit_lidarparse_timestamp; // Timestamp (GStreamer clock time) when this buffer exits g3dlidarparse.
    guint stream_id; // Stream identifier (group-id from STREAM_START) for multi-stream pipelines.
} LidarMeta;

DLS_EXPORT GType lidar_meta_api_get_type(void);
DLS_EXPORT const GstMetaInfo *lidar_meta_get_info(void);

#define LIDAR_META_API_TYPE (lidar_meta_api_get_type())
#define LIDAR_META_INFO (lidar_meta_get_info())

DLS_EXPORT LidarMeta *add_lidar_meta(GstBuffer *buffer, guint lidar_point_count, const std::vector<float> &lidar_data,
                          size_t frame_id, GstClockTime exit_lidarparse_timestamp, guint stream_id);

G_END_DECLS

#endif // G3D_LIDARMETA_H