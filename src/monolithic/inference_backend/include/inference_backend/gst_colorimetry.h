/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/image.h"

#include <gst/video/video.h>

namespace InferenceBackend {

// The colorimetry of a frame with this video info: unknown for formats other than YUV. gst_video_info_from_caps()
// fills in GStreamer's default for the frame size where the caps have no colorimetry; a matrix that the caps leave
// unknown gets that default too.
inline YuvColorimetry YuvColorimetryFromVideoInfo(const GstVideoInfo &info) {
    YuvColorimetry colorimetry;
    if (!GST_VIDEO_INFO_IS_YUV(&info))
        return colorimetry;
    switch (info.colorimetry.matrix) {
    case GST_VIDEO_COLOR_MATRIX_BT601:
        colorimetry.matrix = YuvColorimetry::Matrix::BT601;
        break;
    case GST_VIDEO_COLOR_MATRIX_BT709:
        colorimetry.matrix = YuvColorimetry::Matrix::BT709;
        break;
    case GST_VIDEO_COLOR_MATRIX_BT2020:
        colorimetry.matrix = YuvColorimetry::Matrix::BT2020;
        break;
    case GST_VIDEO_COLOR_MATRIX_SMPTE240M:
        colorimetry.matrix = YuvColorimetry::Matrix::SMPTE240M;
        break;
    case GST_VIDEO_COLOR_MATRIX_FCC:
        colorimetry.matrix = YuvColorimetry::Matrix::FCC;
        break;
    default:
        colorimetry.matrix = YuvColorimetry::DefaultMatrix(GST_VIDEO_INFO_HEIGHT(&info));
        break;
    }
    colorimetry.full_range = info.colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255;
    return colorimetry;
}

// Sets the color matrix and range of the video info of a YUV frame to `colorimetry`. Video infos of other formats,
// and an unknown colorimetry, leave it as it is.
inline void SetYuvColorimetry(GstVideoInfo &info, const YuvColorimetry &colorimetry) {
    if (!GST_VIDEO_INFO_IS_YUV(&info))
        return;
    switch (colorimetry.matrix) {
    case YuvColorimetry::Matrix::BT601:
        info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
        break;
    case YuvColorimetry::Matrix::BT709:
        info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
        break;
    case YuvColorimetry::Matrix::BT2020:
        info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
        break;
    case YuvColorimetry::Matrix::SMPTE240M:
        info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_SMPTE240M;
        break;
    case YuvColorimetry::Matrix::FCC:
        info.colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_FCC;
        break;
    case YuvColorimetry::Matrix::UNKNOWN:
        return;
    }
    info.colorimetry.range = colorimetry.full_range ? GST_VIDEO_COLOR_RANGE_0_255 : GST_VIDEO_COLOR_RANGE_16_235;
}

} // namespace InferenceBackend
