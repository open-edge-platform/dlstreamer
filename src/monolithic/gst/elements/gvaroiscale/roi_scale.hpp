/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/analytics/analytics.h>
#include <gst/video/video.h>

#include <vector>

namespace gvaroiscale {

enum class RoiScaleResult { Ok, NoRoi, Error };

struct Roi {
    GQuark label;
    float confidence;
    gint x;
    gint y;
    gint width;
    gint height;
};

std::vector<Roi> collect_rois(GstBuffer *input_buffer, const GArray *target_classes, guint frame_width,
                              guint frame_height, float confidence_threshold = 0.0f);
// content_rect (optional) receives the letterboxed ROI area inside the output frame.
RoiScaleResult roi_scale_buffer(GstBuffer *input_buffer, const GstVideoInfo &input_info, const Roi &roi,
                                guint output_width, guint output_height, GstBuffer **output_buffer,
                                GstVideoInfo *output_info, GstVideoRectangle *content_rect = nullptr);

} // namespace gvaroiscale