/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "d3d11_converter.h"

#include "inference_backend/pre_proc.h"
#include "safe_arithmetic.hpp"
#include "utils.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

using namespace InferenceBackend;

namespace InferenceBackend {

D3D11Converter::D3D11Converter(D3D11Context *context) : _context(context) {
    if (!context)
        throw std::runtime_error("D3D11Context is null. D3D11Converter requires not nullptr context.");
}

void D3D11Converter::SetupProcessorStreamsWithCustomParams(const InputImageLayerDesc::Ptr &pre_proc_info,
                                                           uint16_t src_width, uint16_t src_height, uint16_t dst_width,
                                                           uint16_t dst_height, RECT &src_rect, RECT &dst_rect,
                                                           D3D11_VIDEO_PROCESSOR_STREAM &stream_params,
                                                           const ImageTransformationParams::Ptr &image_transform_info) {

    // Get current source dimensions from RECT
    uint16_t src_rect_width = safe_convert<uint16_t>(src_rect.right - src_rect.left);
    uint16_t src_rect_height = safe_convert<uint16_t>(src_rect.bottom - src_rect.top);

    // Padding preparations
    uint16_t padding_x = 0;
    uint16_t padding_y = 0;
    uint32_t background_color = 0xff000000;
    if (pre_proc_info->doNeedPadding() && (!image_transform_info || !image_transform_info->WasPadding())) {
        const auto &padding = pre_proc_info->getPadding();
        padding_x = safe_convert<uint16_t>(padding.stride_x);
        padding_y = safe_convert<uint16_t>(padding.stride_y);
        const auto &fill_value = padding.fill_value;
        background_color |=
            static_cast<uint32_t>(fill_value.at(0) * pow(2, 16) + fill_value.at(1) * pow(2, 8) + fill_value.at(2));
    }

    // Initialize destination with padding offset
    dst_rect.left = safe_convert<LONG>(padding_x);
    dst_rect.top = safe_convert<LONG>(padding_y);

    if (padding_x * 2 > dst_width || padding_y * 2 > dst_height) {
        throw std::out_of_range("Invalid padding in relation to size");
    }

    uint16_t input_width_except_padding = dst_width - (padding_x * 2);
    uint16_t input_height_except_padding = dst_height - (padding_y * 2);

    // Working dimensions for destination region
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
        if (pre_proc_info->doNeedCrop() && pre_proc_info->doNeedResize()) {
            additional_crop_scale_param = 1.125;
        }

        if (src_rect_width)
            resize_scale_param_x = safe_convert<double>(input_width_except_padding) / src_rect_width;

        if (src_rect_height)
            resize_scale_param_y = safe_convert<double>(input_height_except_padding) / src_rect_height;

        if (pre_proc_info->getResizeType() == InputImageLayerDesc::Resize::ASPECT_RATIO) {
            resize_scale_param_x = resize_scale_param_y = (std::min)(resize_scale_param_x, resize_scale_param_y);
        }

        resize_scale_param_x *= additional_crop_scale_param;
        resize_scale_param_y *= additional_crop_scale_param;

        // Calculate resized dimensions
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
            uint16_t crop_size = (std::min)(src_width, src_height);
            uint16_t startX = (src_width - crop_size) / 2;
            uint16_t startY = (src_height - crop_size) / 2;

            // Update source rectangle for central crop
            src_rect.left = src_rect.left + safe_convert<LONG>(startX);
            src_rect.top = src_rect.top + safe_convert<LONG>(startY);
            src_rect.right = src_rect.left + safe_convert<LONG>(crop_size);
            src_rect.bottom = src_rect.top + safe_convert<LONG>(crop_size);

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

            // Update destination dimensions
            dst_region_width = cropped_width;
            dst_region_height = cropped_height;

            if (image_transform_info)
                image_transform_info->CropHasDone(cropped_border_x, cropped_border_y);

            // Apply crop to source rectangle (reverse scale for source coordinates)
            cropped_border_x = safe_convert<uint16_t>(safe_convert<double>(cropped_border_x) / resize_scale_param_x);
            cropped_border_y = safe_convert<uint16_t>(safe_convert<double>(cropped_border_y) / resize_scale_param_y);
            cropped_width = safe_convert<uint16_t>(safe_convert<double>(cropped_width) / resize_scale_param_x);
            cropped_height = safe_convert<uint16_t>(safe_convert<double>(cropped_height) / resize_scale_param_y);

            // Apply crop to source rectangle before resize
            src_rect.left += safe_convert<LONG>(cropped_border_x);
            src_rect.top += safe_convert<LONG>(cropped_border_y);
            src_rect.right = src_rect.left + safe_convert<LONG>(cropped_width);
            src_rect.bottom = src_rect.top + safe_convert<LONG>(cropped_height);
        }
    }

    // Add padding for aspect-ratio resize and center the destination region
    dst_rect.left = safe_convert<LONG>((dst_width - dst_region_width) / 2);
    dst_rect.top = safe_convert<LONG>((dst_height - dst_region_height) / 2);
    dst_rect.right = dst_rect.left + safe_convert<LONG>(dst_region_width);
    dst_rect.bottom = dst_rect.top + safe_convert<LONG>(dst_region_height);

    if (image_transform_info)
        image_transform_info->PaddingHasDone(safe_convert<size_t>(dst_rect.left), safe_convert<size_t>(dst_rect.top));

    // Setup D3D11 video processor stream parameters
    ZeroMemory(&stream_params, sizeof(stream_params));
    stream_params.Enable = TRUE;
    stream_params.OutputIndex = 0;
    stream_params.InputFrameOrField = 0;
}

void D3D11Converter::Convert(const Image &src, D3D11Image &d3d11_dst, const InputImageLayerDesc::Ptr &pre_proc_info,
                             const ImageTransformationParams::Ptr &image_transform_info) {

    if (!_context || !_context->VideoContext() || !_context->VideoDevice()) {
        throw std::runtime_error("D3D11Converter::Convert: Invalid D3D11 context");
    }

    // Get D3D11 interfaces
    auto video_context = _context->VideoContext();
    auto video_device = _context->VideoDevice();
    // Create video processor with dynamic dimensions based on actual input/output sizes
    uint32_t input_width = static_cast<uint32_t>(src.width);
    uint32_t input_height = static_cast<uint32_t>(src.height);
    uint32_t output_width = static_cast<uint32_t>(d3d11_dst.image.width);
    uint32_t output_height = static_cast<uint32_t>(d3d11_dst.image.height);

    // Get cached processor (reuse if dimensions match, create if new)
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> video_processor;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> video_processor_enumerator;
    _context->GetCachedVideoProcessor(input_width, input_height, output_width, output_height, video_processor,
                                      video_processor_enumerator);

    // Get source and destination textures
    ID3D11Texture2D *src_texture = nullptr;
    ID3D11Texture2D *dst_texture = reinterpret_cast<ID3D11Texture2D *>(d3d11_dst.image.d3d11_texture);

    if (!dst_texture) {
        throw std::runtime_error("D3D11Converter::Convert: Invalid destination texture");
    }

    if (src.type == MemoryType::D3D11) {
        // Source is already a D3D11 texture
        src_texture = reinterpret_cast<ID3D11Texture2D *>(src.d3d11_texture);
    } else {
        throw std::runtime_error(
            "D3D11Converter::Convert: Unsupported source memory type. Only D3D11 textures supported.");
    }

    if (!src_texture) {
        throw std::runtime_error("D3D11Converter::Convert: Invalid source texture");
    }

    // Create video processor input and output views
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> input_view;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> output_view;

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc = {};
    input_desc.FourCC = 0; // Use texture format
    input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    input_desc.Texture2D.MipSlice = 0;
    input_desc.Texture2D.ArraySlice = 0;

    HRESULT hr = video_device->CreateVideoProcessorInputView(src_texture, video_processor_enumerator.Get(), &input_desc,
                                                             &input_view);
    if (FAILED(hr)) {
        throw std::runtime_error("D3D11Converter::Convert: Failed to create video processor input view");
    }

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc = {};
    output_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    output_desc.Texture2D.MipSlice = 0;

    hr = video_device->CreateVideoProcessorOutputView(dst_texture, video_processor_enumerator.Get(), &output_desc,
                                                      &output_view);
    if (FAILED(hr)) {
        throw std::runtime_error("D3D11Converter::Convert: Failed to create video processor output view");
    }

    // Setup rectangles and stream parameters
    RECT src_rect = {safe_convert<LONG>(src.rect.x), safe_convert<LONG>(src.rect.y),
                     safe_convert<LONG>(src.rect.x + src.rect.width), safe_convert<LONG>(src.rect.y + src.rect.height)};

    RECT dst_rect = {0, 0, safe_convert<LONG>(d3d11_dst.image.width), safe_convert<LONG>(d3d11_dst.image.height)};

    D3D11_VIDEO_PROCESSOR_STREAM stream_params = {};

    if (pre_proc_info && pre_proc_info->isDefined()) {
        // Use custom preprocessing parameters
        SetupProcessorStreamsWithCustomParams(
            pre_proc_info, safe_convert<uint16_t>(src.width), safe_convert<uint16_t>(src.height),
            safe_convert<uint16_t>(d3d11_dst.image.width), safe_convert<uint16_t>(d3d11_dst.image.height), src_rect,
            dst_rect, stream_params, image_transform_info);
    } else {
        // Simple resize - setup basic stream parameters
        ZeroMemory(&stream_params, sizeof(stream_params));
        stream_params.Enable = TRUE;
        stream_params.OutputIndex = 0;
        stream_params.InputFrameOrField = 0;
    }

    // Set source and destination rectangles for the video processor
    video_context->VideoProcessorSetStreamSourceRect(video_processor.Get(), 0, TRUE, &src_rect);
    video_context->VideoProcessorSetStreamDestRect(video_processor.Get(), 0, TRUE, &dst_rect);

    // Set output target rectangle
    RECT output_rect = {0, 0, safe_convert<LONG>(d3d11_dst.image.width), safe_convert<LONG>(d3d11_dst.image.height)};
    video_context->VideoProcessorSetOutputTargetRect(video_processor.Get(), TRUE, &output_rect);

    // Set the background color
    D3D11_VIDEO_COLOR background_color = {};
    background_color.RGBA.R = 0.0f; // Black background
    background_color.RGBA.G = 0.0f;
    background_color.RGBA.B = 0.0f;
    background_color.RGBA.A = 1.0f;
    video_context->VideoProcessorSetOutputBackgroundColor(video_processor.Get(), FALSE, &background_color);

    D3D11_VIDEO_PROCESSOR_STREAM streams[1] = {stream_params};
    streams[0].pInputSurface = input_view.Get();

    // Prepare query for checking conversion completion
    Microsoft::WRL::ComPtr<ID3D11Query> query;
    D3D11_QUERY_DESC query_desc = {};
    query_desc.Query = D3D11_QUERY_EVENT;
    query_desc.MiscFlags = 0;

    /*hr = _context->Device()->CreateQuery(&query_desc, &query);
    if (FAILED(hr)) {
        throw std::runtime_error("D3D11Converter::Convert: Failed to create D3D11 event query");
    }*/

    // Use GStreamer D3D11 device lock for thread-safe DeviceContext access
    // Required per GStreamer documentation: concurrent calls for ID3D11DeviceContext and DXGI API are not allowed
    _context->Lock();

    //_context->DeviceContext()->Begin(query.Get());
    hr = video_context->VideoProcessorBlt(video_processor.Get(),
                                          output_view.Get(), // Output directly to destination texture
                                          0,                 // Output frame
                                          1,                 // Number of input streams
                                          streams);
    if (FAILED(hr)) {
        _context->Unlock();
        throw std::runtime_error("D3D11Converter::Convert: VideoProcessorBlt failed");
    }
    //_context->DeviceContext()->End(query.Get());
    _context->Unlock();

    d3d11_dst.gpu_event_query = query;
}

} // namespace InferenceBackend