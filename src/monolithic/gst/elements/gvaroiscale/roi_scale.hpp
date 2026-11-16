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

// Source-image pixels; a shared ROI uses the aggregate ROI type as its label.
struct Roi {
    GQuark label;
    float confidence;
    gint x;
    gint y;
    gint width;
    gint height;
};

// Crop regions for one frame: the shared ROI from gvaframeselect if it is valid, otherwise the
// detections matching target_classes (empty = all) and confidence_threshold, deduplicated per
// class and sorted by descending confidence. Empty means nothing to crop.
std::vector<Roi> collect_rois(GstBuffer *input_buffer, const GArray *target_classes, guint frame_width,
                              guint frame_height, float confidence_threshold = 0.0f);
// Crops the ROI from an RGB frame and fits it, keeping its aspect ratio, onto a black canvas.
// Ok: the caller owns *output_buffer; content_rect (optional) is where the ROI sits on the canvas.
// NoRoi: the ROI lies outside the frame. Error: invalid input or allocation/mapping failure.
RoiScaleResult roi_scale_buffer(GstBuffer *input_buffer, const GstVideoInfo &input_info, const Roi &roi,
                                guint output_width, guint output_height, GstBuffer **output_buffer,
                                GstVideoInfo *output_info, GstVideoRectangle *content_rect = nullptr);

} // namespace gvaroiscale
