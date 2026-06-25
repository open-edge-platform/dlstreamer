/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __G3D_LIDAR_SRC_H__
#define __G3D_LIDAR_SRC_H__

#include <gst/base/gstpushsrc.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_G3D_LIDAR_SRC (gst_g3d_lidar_src_get_type())
#define GST_G3D_LIDAR_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_G3D_LIDAR_SRC, GstG3DLidarSrc))
#define GST_G3D_LIDAR_SRC_CLASS(klass)                                                                                 \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_G3D_LIDAR_SRC, GstG3DLidarSrcClass))
#define GST_IS_G3D_LIDAR_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_G3D_LIDAR_SRC))
#define GST_IS_G3D_LIDAR_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_G3D_LIDAR_SRC))

typedef struct _GstG3DLidarSrc GstG3DLidarSrc;
typedef struct _GstG3DLidarSrcClass GstG3DLidarSrcClass;
typedef struct _GstG3DLidarSrcPrivate GstG3DLidarSrcPrivate;

struct _GstG3DLidarSrc {
    GstPushSrc parent;

    /* Properties */
    gchar *config;      /* Path to JSON config (vendor/model/transport) */
    gboolean ntp_sync;  /* TRUE = use LiDAR clock, FALSE = use pipeline clock */
    guint64 timeout;    /* Timeout in microseconds (0 = disabled) */

    /* Internal state */
    guint stream_id;
    size_t frame_seq;

    /* Private implementation (rs_driver, frame queue) */
    GstG3DLidarSrcPrivate *priv;
};

struct _GstG3DLidarSrcClass {
    GstPushSrcClass parent_class;
};

GType gst_g3d_lidar_src_get_type(void);

G_END_DECLS

#endif /* __G3D_LIDAR_SRC_H__ */
