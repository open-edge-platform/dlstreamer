/*******************************************************************************
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_GVAWATERMARK3D_H__
#define __GST_GVAWATERMARK3D_H__

#include <gst/video/gstvideofilter.h>
#include <opencv2/opencv.hpp>

G_BEGIN_DECLS

#define GST_TYPE_GVAWATERMARK3D (gst_gvawatermark3d_get_type())
G_DECLARE_FINAL_TYPE(GstGvaWatermark3D, gst_gvawatermark3d, GST, GVAWATERMARK3D, GstVideoFilter)

struct _GstGvaWatermark3D {
    GstVideoFilter parent_instance;
    gchar *intrinsics_file;
    cv::Mat K;
    gchar *calibration_file;
    cv::Mat P2; // 3x4 KITTI projection matrix for camera-frame 3D boxes
};

struct _GstGvaWatermark3DClass {
    GstVideoFilterClass parent_class;
};

G_END_DECLS

#endif /* __GST_GVAWATERMARK3D_H__ */