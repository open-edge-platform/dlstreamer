/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "opencl_preproc_geometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace InferenceBackend {

namespace {

uint8_t ToU8(double value) {
    return static_cast<uint8_t>((std::min)((std::max)(std::lround(value), 0l), 255l));
}

} // namespace

// Follows VaApiConverter::SetupPipelineRegionsWithCustomParams step by step (padding, resize, crop, centering), so
// that the backends agree on what a model-proc means, including the rounding.
OpenclRoiGeometry ComputeRoiGeometry(uint32_t frame_width, uint32_t frame_height, const Rectangle<uint32_t> &rect,
                                     uint32_t dst_width, uint32_t dst_height,
                                     const InputImageLayerDesc::Ptr &pre_proc_info,
                                     const ImageTransformationParams::Ptr &image_transform_info) {
    if (!frame_width || !frame_height || !dst_width || !dst_height)
        throw std::invalid_argument("OpenCL pre-processing: empty frame or model input");

    // The ROI clamped to the frame: ROI coordinates come from detection metadata.
    uint32_t src_x = 0, src_y = 0, src_w = frame_width, src_h = frame_height;
    if (rect.width > 0 && rect.height > 0) {
        src_x = (std::min)(rect.x, frame_width - 1);
        src_y = (std::min)(rect.y, frame_height - 1);
        src_w = (std::min)(rect.width, frame_width - src_x);
        src_h = (std::min)(rect.height, frame_height - src_y);
    }

    OpenclRoiGeometry g;
    g.src_x = src_x;
    g.src_y = src_y;
    g.src_w = src_w;
    g.src_h = src_h;
    const int dst_w = static_cast<int>(dst_width);
    const int dst_h = static_cast<int>(dst_height);

    if (!pre_proc_info || !pre_proc_info->isDefined()) {
        // Just a resize of the region to the whole model input.
        g.dst_w = dst_w;
        g.dst_h = dst_h;
        return g;
    }

    // Padding
    int padding_x = 0, padding_y = 0;
    if (pre_proc_info->doNeedPadding() && (!image_transform_info || !image_transform_info->WasPadding())) {
        const auto &padding = pre_proc_info->getPadding();
        padding_x = static_cast<int>(padding.stride_x);
        padding_y = static_cast<int>(padding.stride_y);
        const auto &fill = padding.fill_value;
        for (size_t c = 0; c < 3 && !fill.empty(); ++c)
            g.fill_rgb[c] = ToU8(fill.size() >= 3 ? fill[c] : fill[0]);
    }
    if (padding_x * 2 > dst_w || padding_y * 2 > dst_h)
        throw std::out_of_range("Invalid padding in relation to size");
    const int input_w = dst_w - padding_x * 2;
    const int input_h = dst_h - padding_y * 2;

    // Resize. Central-resize crops the source directly, so the regular resize step does not run before it.
    const bool central_resize =
        pre_proc_info->doNeedCrop() && pre_proc_info->getCropType() == InputImageLayerDesc::Crop::CENTRAL_RESIZE;
    const auto resize_type = pre_proc_info->getResizeType();
    int region_w = static_cast<int>(src_w);
    int region_h = static_cast<int>(src_h);
    double scale_x = 1, scale_y = 1;
    if (!central_resize && pre_proc_info->doNeedResize() &&
        (static_cast<int>(src_w) != input_w || static_cast<int>(src_h) != input_h)) {
        if (pre_proc_info->isAspectRatioMultipleOfResize())
            throw std::runtime_error("Aspect-ratio-multiple-of resize is supported only by the OpenCV preprocessor");
        const double crop_scale = pre_proc_info->doNeedCrop() ? 1.125 : 1.0;
        scale_x = static_cast<double>(input_w) / src_w;
        scale_y = static_cast<double>(input_h) / src_h;
        if (resize_type == InputImageLayerDesc::Resize::ASPECT_RATIO ||
            resize_type == InputImageLayerDesc::Resize::ASPECT_RATIO_PAD)
            scale_x = scale_y = (std::min)(scale_x, scale_y);
        scale_x *= crop_scale;
        scale_y *= crop_scale;
        region_w = static_cast<int>(src_w * scale_x + 0.5);
        region_h = static_cast<int>(src_h * scale_y + 0.5);
        if (image_transform_info)
            image_transform_info->ResizeHasDone(scale_x, scale_y);
    }

    // Crop
    const int region_w_before_crop = region_w;
    const int region_h_before_crop = region_h;
    if (pre_proc_info->doNeedCrop() && (region_w != input_w || region_h != input_h)) {
        int border_x = region_w > input_w ? region_w - input_w : 0;
        int border_y = region_h > input_h ? region_h - input_h : 0;
        const int cropped_w = region_w - border_x;
        const int cropped_h = region_h - border_y;

        if (central_resize) {
            // Center-crop of the region to the aspect ratio of the model input, then resized to it.
            const auto crop = InputImageLayerDesc::CalculateCentralResizeCrop(src_w, src_h, input_w, input_h);
            g.src_x = src_x + static_cast<double>(crop.x);
            g.src_y = src_y + static_cast<double>(crop.y);
            g.src_w = static_cast<double>(crop.width);
            g.src_h = static_cast<double>(crop.height);
            region_w = input_w;
            region_h = input_h;
            if (image_transform_info)
                image_transform_info->CropThenResizeHasDone(crop.x, crop.y, static_cast<double>(input_w) / crop.width,
                                                            static_cast<double>(input_h) / crop.height);
        } else {
            switch (pre_proc_info->getCropType()) {
            case InputImageLayerDesc::Crop::CENTRAL:
                border_x /= 2;
                border_y /= 2;
                break;
            case InputImageLayerDesc::Crop::TOP_LEFT:
                border_x = 0;
                border_y = 0;
                break;
            case InputImageLayerDesc::Crop::TOP_RIGHT:
                border_y = 0;
                break;
            case InputImageLayerDesc::Crop::BOTTOM_LEFT:
                border_x = 0;
                break;
            case InputImageLayerDesc::Crop::BOTTOM_RIGHT:
                break;
            default:
                throw std::runtime_error("Unknown crop format.");
            }
            region_w = cropped_w;
            region_h = cropped_h;
            if (image_transform_info)
                image_transform_info->CropHasDone(static_cast<size_t>(border_x), static_cast<size_t>(border_y));

            // The crop is done on the source, before the resize, in one pass: mapped back with the scale the
            // resized region really has (its size is rounded), so that the source stays inside the region.
            const double region_scale_x = static_cast<double>(region_w_before_crop) / src_w;
            const double region_scale_y = static_cast<double>(region_h_before_crop) / src_h;
            g.src_x += border_x / region_scale_x;
            g.src_y += border_y / region_scale_y;
            g.src_w = cropped_w / region_scale_x;
            g.src_h = cropped_h / region_scale_y;
        }
    }

    // Centered (top-left for aspect-ratio-pad), the rest is padding.
    if (resize_type == InputImageLayerDesc::Resize::ASPECT_RATIO_PAD) {
        g.dst_x = 0;
        g.dst_y = 0;
    } else {
        g.dst_x = (dst_w - region_w) / 2;
        g.dst_y = (dst_h - region_h) / 2;
    }
    g.dst_w = region_w;
    g.dst_h = region_h;
    if (image_transform_info)
        image_transform_info->PaddingHasDone(static_cast<size_t>((std::max)(g.dst_x, 0)),
                                             static_cast<size_t>((std::max)(g.dst_y, 0)));
    if (g.dst_w <= 0 || g.dst_h <= 0 || g.src_w <= 0 || g.src_h <= 0)
        throw std::runtime_error("OpenCL pre-processing: empty source or destination region");
    return g;
}

} // namespace InferenceBackend
