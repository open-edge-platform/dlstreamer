/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef G3D_LIDARMETA_H
#define G3D_LIDARMETA_H

#include <cstddef>
#include <gst/gst.h>
#include <vector>

G_BEGIN_DECLS

typedef struct _LidarMeta {
    GstMeta meta;
    guint lidar_point_count;
    std::vector<float> lidar_data;
    size_t frame_id;
    GstClockTime exit_lidarparse_timestamp;
    guint stream_id;
} LidarMeta;

GType lidar_meta_api_get_type(void);
const GstMetaInfo *lidar_meta_get_info(void);

#define LIDAR_META_API_TYPE (lidar_meta_api_get_type())
#define LIDAR_META_INFO (lidar_meta_get_info())

LidarMeta *add_lidar_meta(GstBuffer *buffer, guint lidar_point_count, const std::vector<float> &lidar_data,
                          size_t frame_id, GstClockTime exit_lidarparse_timestamp, guint stream_id);

G_END_DECLS

#endif // G3D_LIDARMETA_H