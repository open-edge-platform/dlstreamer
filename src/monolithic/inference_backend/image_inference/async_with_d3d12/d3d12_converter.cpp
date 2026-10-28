/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "d3d12_converter.h"

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

// GstD3D12Converter "border-color" is ARGB with 16 bits per channel.
uint64_t MakeBorderColor(uint8_t r, uint8_t g, uint8_t b) {
    auto expand = [](uint8_t v) { return static_cast<uint64_t>(v) * 257u; };
    return (0xffffULL << 48) | (expand(r) << 32) | (expand(g) << 16) | expand(b);
}

void check_hr(HRESULT hr, const char *msg) {
    if (FAILED(hr))
        throw std::runtime_error(std::string("D3D12Converter: ") + msg + " (HRESULT 0x" +
                                 std::to_string(static_cast<unsigned long>(hr)) + ")");
}

} // namespace

D3D12Converter::D3D12Converter(GstD3D12Device *device, uint32_t src_width, uint32_t src_height, DXGI_FORMAT src_format,
                               uint32_t dst_width, uint32_t dst_height, DXGI_FORMAT dst_format)
    : _device(device), _src_width(src_width), _src_height(src_height), _dst_width(dst_width), _dst_height(dst_height),
      _src_format(src_format) {

    if (!device)
        throw std::invalid_argument("D3D12Converter: device is null");

    GstVideoFormat gst_src_fmt = DXGIFormatToGst(src_format);
    GstVideoFormat gst_dst_fmt = DXGIFormatToGst(dst_format);

    if (gst_src_fmt == GST_VIDEO_FORMAT_UNKNOWN || gst_dst_fmt == GST_VIDEO_FORMAT_UNKNOWN)
        throw std::invalid_argument("D3D12Converter: unsupported format");

    GstVideoInfo in_info, out_info;
    gst_video_info_set_format(&in_info, gst_src_fmt, src_width, src_height);
    gst_video_info_set_format(&out_info, gst_dst_fmt, dst_width, dst_height);

    _queue = gst_d3d12_device_get_cmd_queue(_device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (!_queue)
        throw std::runtime_error("D3D12Converter: no DIRECT command queue");
    _fence = gst_d3d12_cmd_queue_get_fence_handle(_queue);

    // Queue = nullptr selects the device's DIRECT queue, which is also the one we execute on.
    _converter = gst_d3d12_converter_new(_device, nullptr, &in_info, &out_info, nullptr, nullptr, nullptr);
    if (!_converter)
        throw std::runtime_error("D3D12Converter: Failed to create GstD3D12Converter");

    g_object_set(_converter, "fill-border", TRUE, nullptr);

    auto *dev_handle = gst_d3d12_device_get_device_handle(_device);
    _ca_pool = gst_d3d12_cmd_alloc_pool_new(dev_handle, D3D12_COMMAND_LIST_TYPE_DIRECT);
    _fence_data_pool = gst_d3d12_fence_data_pool_new();
    if (!_ca_pool || !_fence_data_pool)
        throw std::runtime_error("D3D12Converter: Failed to create command allocator / fence data pool");

    GVA_INFO("D3D12Converter created: %ux%u (fmt=%u) -> %ux%u (fmt=%u)", src_width, src_height, src_format, dst_width,
             dst_height, dst_format);
}

D3D12Converter::~D3D12Converter() {
    // The command allocators recycled through fence notifications must not outlive
    // the pool, so drain what we submitted first.
    if (_queue && _last_fence_value)
        gst_d3d12_cmd_queue_fence_wait(_queue, _last_fence_value);
    _cl.Reset();
    if (_fence_data_pool)
        gst_object_unref(_fence_data_pool);
    if (_ca_pool)
        gst_object_unref(_ca_pool);
    if (_converter)
        gst_object_unref(_converter);
}

void D3D12Converter::SetupPreprocessing(const InputImageLayerDesc::Ptr &pre_proc_info,
                                        const ImageTransformationParams::Ptr &image_transform_info, uint32_t src_rect_x,
                                        uint32_t src_rect_y, uint32_t src_rect_w, uint32_t src_rect_h) {
    uint16_t src_rect_width = safe_convert<uint16_t>(src_rect_w);
    uint16_t src_rect_height = safe_convert<uint16_t>(src_rect_h);

    // Padding preparations
    uint16_t padding_x = 0;
    uint16_t padding_y = 0;
    uint64_t background_color = MakeBorderColor(0, 0, 0);
    if (pre_proc_info->doNeedPadding() && (!image_transform_info || !image_transform_info->WasPadding())) {
        const auto &padding = pre_proc_info->getPadding();
        padding_x = safe_convert<uint16_t>(padding.stride_x);
        padding_y = safe_convert<uint16_t>(padding.stride_y);
        const auto &fill_value = padding.fill_value;
        background_color =
            MakeBorderColor(static_cast<uint8_t>(fill_value.at(0)), static_cast<uint8_t>(fill_value.at(1)),
                            static_cast<uint8_t>(fill_value.at(2)));
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
        if (pre_proc_info->isAspectRatioMultipleOfResize()) {
            throw std::runtime_error("Aspect-ratio-multiple-of resize is supported only by the OpenCV preprocessor");
        }
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

            src_rect_x = startX;
            src_rect_y = startY;
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

    int dest_x = (_dst_width - dst_region_width) / 2;
    int dest_y = (_dst_height - dst_region_height) / 2;

    if (image_transform_info)
        image_transform_info->PaddingHasDone(dest_x, dest_y);

    g_object_set(_converter, "src-x", static_cast<gint>(src_rect_x), "src-y", static_cast<gint>(src_rect_y),
                 "src-width", static_cast<gint>(src_rect_w), "src-height", static_cast<gint>(src_rect_h), "dest-x",
                 static_cast<gint>(dest_x), "dest-y", static_cast<gint>(dest_y), "dest-width",
                 static_cast<gint>(dst_region_width), "dest-height", static_cast<gint>(dst_region_height),
                 "border-color", background_color, nullptr);
}

uint64_t D3D12Converter::Convert(GstBuffer *src_buffer, GstBuffer *dst_buffer,
                                 const InputImageLayerDesc::Ptr &pre_proc_info,
                                 const ImageTransformationParams::Ptr &image_transform_info,
                                 const Rectangle<uint32_t> &roi) {
    if (!src_buffer || !dst_buffer)
        throw std::invalid_argument("D3D12Converter::Convert: null buffer");

    std::lock_guard lk(_mutex);

    // Source rect = logical frame size; decoder DPB textures are padded to the codec
    // macroblock alignment, so the resource desc must not be used.
    uint32_t src_rect_x = 0;
    uint32_t src_rect_y = 0;
    uint32_t src_rect_w = _src_width;
    uint32_t src_rect_h = _src_height;

    if (roi.width > 0 && roi.height > 0) {
        // Clamp to the frame: ROI coordinates come from detection metadata and may be
        // rounded outside the frame by a pixel.
        src_rect_x = (std::min)(roi.x, _src_width);
        src_rect_y = (std::min)(roi.y, _src_height);
        src_rect_w = (std::min)(roi.width, _src_width - src_rect_x);
        src_rect_h = (std::min)(roi.height, _src_height - src_rect_y);
    }

    if (src_rect_w == 0 || src_rect_h == 0)
        throw std::runtime_error("D3D12Converter::Convert: empty source region");

    if (pre_proc_info && pre_proc_info->isDefined()) {
        SetupPreprocessing(pre_proc_info, image_transform_info, src_rect_x, src_rect_y, src_rect_w, src_rect_h);
    } else {
        // Simple resize: source region (ROI or full frame) to full destination
        g_object_set(_converter, "src-x", static_cast<gint>(src_rect_x), "src-y", static_cast<gint>(src_rect_y),
                     "src-width", static_cast<gint>(src_rect_w), "src-height", static_cast<gint>(src_rect_h), "dest-x",
                     0, "dest-y", 0, "dest-width", static_cast<gint>(_dst_width), "dest-height",
                     static_cast<gint>(_dst_height), nullptr);
    }

    GstD3D12CmdAlloc *gst_ca = nullptr;
    if (!gst_d3d12_cmd_alloc_pool_acquire(_ca_pool, &gst_ca))
        throw std::runtime_error("D3D12Converter::Convert: couldn't acquire command allocator");
    auto *ca = gst_d3d12_cmd_alloc_get_handle(gst_ca);

    HRESULT hr = ca->Reset();
    if (FAILED(hr)) {
        gst_d3d12_cmd_alloc_unref(gst_ca);
        check_hr(hr, "command allocator Reset");
    }

    if (!_cl) {
        auto *dev = gst_d3d12_device_get_device_handle(_device);
        hr = dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca, nullptr, IID_PPV_ARGS(&_cl));
    } else {
        hr = _cl->Reset(ca, nullptr);
    }
    if (FAILED(hr)) {
        gst_d3d12_cmd_alloc_unref(gst_ca);
        check_hr(hr, "command list Reset");
    }

    GstD3D12FenceData *fence_data = nullptr;
    gst_d3d12_fence_data_pool_acquire(_fence_data_pool, &fence_data);
    gst_d3d12_fence_data_push(fence_data, gst_ca, reinterpret_cast<GDestroyNotify>(gst_d3d12_cmd_alloc_unref));

    // execute_gpu_wait=TRUE: the DIRECT queue waits for fences attached to src (decoder)
    // and dst (previous consumer) memories before the conversion runs.
    // On failure the list must not stay open: its allocator goes back to the pool with
    // fence_data, and the next Reset of either the list or that allocator would fail,
    // breaking every later frame. Drop the list so the next Convert creates a new one.
    if (!gst_d3d12_converter_convert_buffer(_converter, src_buffer, dst_buffer, fence_data, _cl.Get(), TRUE)) {
        _cl->Close();
        _cl.Reset();
        gst_d3d12_fence_data_unref(fence_data);
        throw std::runtime_error("D3D12Converter::Convert: gst_d3d12_converter_convert_buffer failed");
    }

    hr = _cl->Close();
    if (FAILED(hr)) {
        _cl.Reset();
        gst_d3d12_fence_data_unref(fence_data);
        check_hr(hr, "command list Close");
    }

    ID3D12CommandList *lists[] = {_cl.Get()};
    uint64_t fence_value = 0;
    hr = gst_d3d12_cmd_queue_execute_command_lists(_queue, 1, lists, &fence_value);
    if (FAILED(hr)) {
        gst_d3d12_fence_data_unref(fence_data);
        check_hr(hr, "ExecuteCommandLists");
    }
    _last_fence_value = fence_value;

    gst_d3d12_buffer_set_fence(dst_buffer, _fence, fence_value, FALSE);
    gst_d3d12_cmd_queue_set_notify(_queue, fence_value, fence_data,
                                   reinterpret_cast<GDestroyNotify>(gst_d3d12_fence_data_unref));
    return fence_value;
}
