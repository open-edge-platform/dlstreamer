/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/input_image_layer_descriptor.h"

#include <memory>

#include <gst/d3d11/gstd3d11.h>

namespace InferenceBackend {

class D3D11Converter {
  public:
    D3D11Converter(GstD3D11Device *device, uint32_t src_width, uint32_t src_height, DXGI_FORMAT src_format,
                   uint32_t dst_width, uint32_t dst_height, DXGI_FORMAT dst_format);
    ~D3D11Converter();

    D3D11Converter(const D3D11Converter &) = delete;
    D3D11Converter &operator=(const D3D11Converter &) = delete;

    void Convert(GstBuffer *src_buffer, GstBuffer *dst_buffer, const InputImageLayerDesc::Ptr &pre_proc_info = nullptr,
                 const ImageTransformationParams::Ptr &image_transform_info = nullptr);

  private:
    GstD3D11Device *_device;
    GstD3D11Converter *_converter = nullptr;
    uint32_t _src_width, _src_height;
    uint32_t _dst_width, _dst_height;

    void SetupPreprocessing(const InputImageLayerDesc::Ptr &pre_proc_info,
                            const ImageTransformationParams::Ptr &image_transform_info, uint32_t src_rect_x,
                            uint32_t src_rect_y, uint32_t src_rect_w, uint32_t src_rect_h);
};

} // namespace InferenceBackend
