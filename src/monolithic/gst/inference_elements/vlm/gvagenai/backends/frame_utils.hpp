/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>

#include "dlstreamer/gst/mappers/gst_to_cpu.h"

#include <openvino/runtime/tensor.hpp>

namespace genai {

/**
 * @brief Map a GStreamer buffer to an RGB u8 OpenVINO tensor
 *
 * Handles the common video formats (RGB/RGBA/BGR/BGRA/NV12/I420), converting to
 * packed RGB. The returned tensor has shape {1, H, W, C} and owns its pixel data
 * (copied out of the mapped buffer), so the source GstBuffer may be released
 * immediately after this call.
 *
 * @param mapper Reusable GST->CPU memory mapper (one per element)
 * @param buffer GStreamer buffer containing the video frame
 * @param info Video format information
 * @return Owning RGB tensor of shape {1, H, W, C}
 * @throws std::runtime_error if the format is unsupported or conversion fails
 */
ov::Tensor gst_buffer_to_rgb_tensor(dlstreamer::MemoryMapperGSTToCPU &mapper, GstBuffer *buffer, GstVideoInfo *info);

} // namespace genai
