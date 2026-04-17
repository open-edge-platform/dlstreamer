/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "d3d11_converter.h"

#include "inference_backend/logger.h"
#include "inference_backend/pre_proc.h"
#include "safe_arithmetic.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

using namespace InferenceBackend;

namespace {

GstVideoFormat DXGIFormatToGst(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_NV12:
        return GST_VIDEO_FORMAT_NV12;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return GST_VIDEO_FORMAT_BGRA;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return GST_VIDEO_FORMAT_RGBA;
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        return GST_VIDEO_FORMAT_BGRx;
    default:
        return GST_VIDEO_FORMAT_UNKNOWN;
    }
}

} // namespace

D3D11Converter::D3D11Converter(GstD3D11Device *device, uint32_t src_width, uint32_t src_height, DXGI_FORMAT src_format,
                               uint32_t dst_width, uint32_t dst_height, DXGI_FORMAT dst_format)
    : _device(device), _src_width(src_width), _src_height(src_height), _dst_width(dst_width), _dst_height(dst_height) {

    if (!device)
        throw std::invalid_argument("D3D11Converter: device is null");

    GstVideoFormat gst_src_fmt = DXGIFormatToGst(src_format);
    GstVideoFormat gst_dst_fmt = DXGIFormatToGst(dst_format);

    if (gst_src_fmt == GST_VIDEO_FORMAT_UNKNOWN || gst_dst_fmt == GST_VIDEO_FORMAT_UNKNOWN)
        throw std::invalid_argument("D3D11Converter: unsupported format");

    GstVideoInfo in_info, out_info;
    gst_video_info_set_format(&in_info, gst_src_fmt, src_width, src_height);
    gst_video_info_set_format(&out_info, gst_dst_fmt, dst_width, dst_height);

    // Use video processor backend
    GstStructure *config =
        gst_structure_new("converter-config", GST_D3D11_CONVERTER_OPT_BACKEND, GST_TYPE_D3D11_CONVERTER_BACKEND,
                          static_cast<guint>(GST_D3D11_CONVERTER_BACKEND_VIDEO_PROCESSOR), nullptr);

    _converter = gst_d3d11_converter_new(_device, &in_info, &out_info, config);
    if (!_converter)
        throw std::runtime_error("D3D11Converter: Failed to create GstD3D11Converter");

    // Enable fill-border for padding support
    g_object_set(_converter, "fill-border", TRUE, nullptr);

    GVA_INFO("D3D11Converter created: %ux%u (fmt=%u) -> %ux%u (fmt=%u)", src_width, src_height, src_format, dst_width,
             dst_height, dst_format);
}

D3D11Converter::~D3D11Converter() {
    if (_converter) {
        gst_object_unref(_converter);
        _converter = nullptr;
    }
}

void D3D11Converter::SetupPreprocessing(const InputImageLayerDesc::Ptr &pre_proc_info,
                                        const ImageTransformationParams::Ptr &image_transform_info, uint32_t src_rect_x,
                                        uint32_t src_rect_y, uint32_t src_rect_w, uint32_t src_rect_h) {
    uint16_t src_rect_width = safe_convert<uint16_t>(src_rect_w);
    uint16_t src_rect_height = safe_convert<uint16_t>(src_rect_h);

    // Padding preparations
    uint16_t padding_x = 0;
    uint16_t padding_y = 0;
    uint64_t background_color = 0xff000000ULL;
    if (pre_proc_info->doNeedPadding() && (!image_transform_info || !image_transform_info->WasPadding())) {
        const auto &padding = pre_proc_info->getPadding();
        padding_x = safe_convert<uint16_t>(padding.stride_x);
        padding_y = safe_convert<uint16_t>(padding.stride_y);
        const auto &fill_value = padding.fill_value;
        background_color |= (static_cast<uint64_t>(fill_value.at(0)) << 16) |
                            (static_cast<uint64_t>(fill_value.at(1)) << 8) | static_cast<uint64_t>(fill_value.at(2));
    }

    if (padding_x * 2 > _dst_width || padding_y * 2 > _dst_height)
        throw std::out_of_range("Invalid padding in relation to size");

    uint16_t input_width_except_padding = _dst_width - (padding_x * 2);
    uint16_t input_height_except_padding = _dst_height - (padding_y * 2);

    uint16_t dst_region_width = src_rect_width;
    uint16_t dst_region_height = src_rect_height;

    // Resize preparations
    double resize_scale_param_x = 1;
    double resize_scale_param_y = 1;
    if (pre_proc_info->doNeedResize() &&
        (src_rect_width != input_width_except_padding || src_rect_height != input_height_except_padding)) {
        double additional_crop_scale_param = 1;
        if (pre_proc_info->doNeedCrop() && pre_proc_info->doNeedResize())
            additional_crop_scale_param = 1.125;

        if (src_rect_width)
            resize_scale_param_x = safe_convert<double>(input_width_except_padding) / src_rect_width;
        if (src_rect_height)
            resize_scale_param_y = safe_convert<double>(input_height_except_padding) / src_rect_height;

        if (pre_proc_info->getResizeType() == InputImageLayerDesc::Resize::ASPECT_RATIO)
            resize_scale_param_x = resize_scale_param_y = (std::min)(resize_scale_param_x, resize_scale_param_y);

        resize_scale_param_x *= additional_crop_scale_param;
        resize_scale_param_y *= additional_crop_scale_param;

        dst_region_width = safe_convert<uint16_t>(src_rect_width * resize_scale_param_x + 0.5);
        dst_region_height = safe_convert<uint16_t>(src_rect_height * resize_scale_param_y + 0.5);

        if (image_transform_info)
            image_transform_info->ResizeHasDone(resize_scale_param_x, resize_scale_param_y);
    }

    // Crop preparations
    if (pre_proc_info->doNeedCrop() &&
        (dst_region_width != input_width_except_padding || dst_region_height != input_height_except_padding)) {
        uint16_t cropped_border_x = 0;
        uint16_t cropped_border_y = 0;

        if (dst_region_width > input_width_except_padding)
            cropped_border_x = dst_region_width - input_width_except_padding;
        if (dst_region_height > input_height_except_padding)
            cropped_border_y = dst_region_height - input_height_except_padding;

        uint16_t cropped_width = dst_region_width - cropped_border_x;
        uint16_t cropped_height = dst_region_height - cropped_border_y;

        if (pre_proc_info->getCropType() == InputImageLayerDesc::Crop::CENTRAL_RESIZE) {
            uint16_t crop_size = (std::min)(static_cast<uint16_t>(_src_width), static_cast<uint16_t>(_src_height));
            uint16_t startX = (_src_width - crop_size) / 2;
            uint16_t startY = (_src_height - crop_size) / 2;

            src_rect_x += startX;
            src_rect_y += startY;
            src_rect_w = crop_size;
            src_rect_h = crop_size;

            dst_region_width = input_width_except_padding;
            dst_region_height = input_height_except_padding;

            if (image_transform_info)
                image_transform_info->CropHasDone(startX, startY);
        } else {
            switch (pre_proc_info->getCropType()) {
            case InputImageLayerDesc::Crop::CENTRAL:
                cropped_border_x /= 2;
                cropped_border_y /= 2;
                break;
            case InputImageLayerDesc::Crop::TOP_LEFT:
                cropped_border_x = 0;
                cropped_border_y = 0;
                break;
            case InputImageLayerDesc::Crop::TOP_RIGHT:
                cropped_border_y = 0;
                break;
            case InputImageLayerDesc::Crop::BOTTOM_LEFT:
                cropped_border_x = 0;
                break;
            case InputImageLayerDesc::Crop::BOTTOM_RIGHT:
                break;
            default:
                throw std::runtime_error("Unknown crop format.");
            }

            dst_region_width = cropped_width;
            dst_region_height = cropped_height;

            if (image_transform_info)
                image_transform_info->CropHasDone(cropped_border_x, cropped_border_y);

            // Apply crop to source rectangle (reverse scale for source coordinates)
            uint16_t src_crop_x = safe_convert<uint16_t>(safe_convert<double>(cropped_border_x) / resize_scale_param_x);
            uint16_t src_crop_y = safe_convert<uint16_t>(safe_convert<double>(cropped_border_y) / resize_scale_param_y);
            uint16_t src_crop_w = safe_convert<uint16_t>(safe_convert<double>(cropped_width) / resize_scale_param_x);
            uint16_t src_crop_h = safe_convert<uint16_t>(safe_convert<double>(cropped_height) / resize_scale_param_y);

            src_rect_x += src_crop_x;
            src_rect_y += src_crop_y;
            src_rect_w = src_crop_w;
            src_rect_h = src_crop_h;
        }
    }

    // Compute final destination rect (centered with padding)
    int dest_x = (_dst_width - dst_region_width) / 2;
    int dest_y = (_dst_height - dst_region_height) / 2;

    if (image_transform_info)
        image_transform_info->PaddingHasDone(dest_x, dest_y);

    // Apply to GstD3D11Converter properties
    g_object_set(_converter, "src-x", static_cast<gint>(src_rect_x), "src-y", static_cast<gint>(src_rect_y),
                 "src-width", static_cast<gint>(src_rect_w), "src-height", static_cast<gint>(src_rect_h), "dest-x",
                 static_cast<gint>(dest_x), "dest-y", static_cast<gint>(dest_y), "dest-width",
                 static_cast<gint>(dst_region_width), "dest-height", static_cast<gint>(dst_region_height),
                 "border-color", background_color, nullptr);
}

void D3D11Converter::Convert(GstBuffer *src_buffer, GstBuffer *dst_buffer,
                             const InputImageLayerDesc::Ptr &pre_proc_info,
                             const ImageTransformationParams::Ptr &image_transform_info) {
    if (!src_buffer || !dst_buffer)
        throw std::invalid_argument("D3D11Converter::Convert: null buffer");

    // Get source dimensions from the GstBuffer's D3D11 memory
    GstMemory *src_mem = gst_buffer_peek_memory(src_buffer, 0);
    auto *src_d3d11_mem = reinterpret_cast<GstD3D11Memory *>(src_mem);
    D3D11_TEXTURE2D_DESC src_desc;
    gst_d3d11_memory_get_texture_desc(src_d3d11_mem, &src_desc);

    uint32_t src_rect_x = 0;
    uint32_t src_rect_y = 0;
    uint32_t src_rect_w = src_desc.Width;
    uint32_t src_rect_h = src_desc.Height;

    if (pre_proc_info && pre_proc_info->isDefined()) {
        SetupPreprocessing(pre_proc_info, image_transform_info, src_rect_x, src_rect_y, src_rect_w, src_rect_h);
    } else {
        // Simple resize: full source to full destination
        g_object_set(_converter, "src-x", 0, "src-y", 0, "src-width", static_cast<gint>(src_desc.Width), "src-height",
                     static_cast<gint>(src_desc.Height), "dest-x", 0, "dest-y", 0, "dest-width",
                     static_cast<gint>(_dst_width), "dest-height", static_cast<gint>(_dst_height), nullptr);
    }

    // GstD3D11Converter handles device locking internally
    if (!gst_d3d11_converter_convert_buffer(_converter, src_buffer, dst_buffer)) {
        throw std::runtime_error("D3D11Converter::Convert: gst_d3d11_converter_convert_buffer failed");
    }
}
