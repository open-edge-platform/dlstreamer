/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/image.h"
#include "inference_backend/input_image_layer_descriptor.h"

#include <cstdint>

namespace InferenceBackend {

// Which part of the frame goes where in the model input.
struct OpenclRoiGeometry {
    // Source rectangle, in frame pixels.
    double src_x = 0, src_y = 0, src_w = 0, src_h = 0;
    // Destination rectangle, in model input pixels. The rest of the model input is padding.
    int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;
    // Padding color as R, G, B.
    uint8_t fill_rgb[3] = {0, 0, 0};
};

// Maps `rect` of a frame_width x frame_height frame (the whole frame if `rect` is empty; clamped to the frame) to a
// dst_width x dst_height model input, as the model-proc pre-processing parameters in `pre_proc_info` ask: resize with
// or without keeping the aspect ratio, crop, padding. Without parameters the region is stretched over the whole
// input. Reports what was done to `image_transform_info`, if given, for post-processing to map results back.
//
// The VA backend (VaApiConverter) computes the same regions and reports the same parameters, with two exceptions:
// a crop is mapped back to the source with fractional precision and the scale of the resized (rounded) region
// instead of being truncated to whole pixels, so that it stays inside the region, and
// "aspect-ratio-pad" resize keeps the aspect ratio and places the image at the top-left, as the OpenCV backend does.
OpenclRoiGeometry ComputeRoiGeometry(uint32_t frame_width, uint32_t frame_height, const Rectangle<uint32_t> &rect,
                                     uint32_t dst_width, uint32_t dst_height,
                                     const InputImageLayerDesc::Ptr &pre_proc_info,
                                     const ImageTransformationParams::Ptr &image_transform_info);

} // namespace InferenceBackend
