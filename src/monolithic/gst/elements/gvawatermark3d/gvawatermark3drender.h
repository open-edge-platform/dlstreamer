/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_GVAWATERMARK3D_RENDER_H__
#define __GST_GVAWATERMARK3D_RENDER_H__

#include <gst/video/gstvideofilter.h>
#include <opencv2/opencv.hpp>

G_BEGIN_DECLS

// Hidden inner drawing filter for the gvawatermark3d bin. Not registered as a public element;
// instantiated internally via g_object_new. Requires system-memory BGR input.
#define GST_TYPE_GVA_WATERMARK3D_RENDER (gst_gva_watermark3d_render_get_type())
G_DECLARE_FINAL_TYPE(GstGvaWatermark3DRender, gst_gva_watermark3d_render, GST, GVA_WATERMARK3D_RENDER, GstVideoFilter)

struct _GstGvaWatermark3DRender {
    GstVideoFilter parent_instance;
    gchar *intrinsics_file;
    cv::Mat K;
    gchar *calibration_file;
    cv::Mat P2; // 3x4 KITTI projection matrix for camera-frame 3D boxes
};

struct _GstGvaWatermark3DRenderClass {
    GstVideoFilterClass parent_class;
};

G_END_DECLS

#endif /* __GST_GVAWATERMARK3D_RENDER_H__ */