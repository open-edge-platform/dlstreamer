/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/image.h"
#include "inference_backend/input_image_layer_descriptor.h"

#include <memory>
#include <mutex>

#include <gst/d3d12/gstd3d12.h>
#include <wrl/client.h>

namespace InferenceBackend {

// GstD3D12Converter wrapper: records the conversion into its own command list and
// executes it on the device's DIRECT queue. Unlike D3D11 there is no immediate
// context, so completion is tracked through the DIRECT queue fence; Convert()
// returns the fence value and tags the destination buffer with it.
class D3D12Converter {
  public:
    D3D12Converter(GstD3D12Device *device, uint32_t src_width, uint32_t src_height, DXGI_FORMAT src_format,
                   uint32_t dst_width, uint32_t dst_height, DXGI_FORMAT dst_format);
    ~D3D12Converter();

    D3D12Converter(const D3D12Converter &) = delete;
    D3D12Converter &operator=(const D3D12Converter &) = delete;

    // Returns the DIRECT-queue fence value signalled once the conversion has completed.
    // roi selects the source sub-rectangle to preprocess; a zero-sized roi means the whole frame.
    uint64_t Convert(GstBuffer *src_buffer, GstBuffer *dst_buffer,
                     const InputImageLayerDesc::Ptr &pre_proc_info = nullptr,
                     const ImageTransformationParams::Ptr &image_transform_info = nullptr,
                     const Rectangle<uint32_t> &roi = {});

    ID3D12Fence *fence() const {
        return _fence;
    }

  private:
    GstD3D12Device *_device;
    GstD3D12Converter *_converter = nullptr;
    GstD3D12CmdQueue *_queue = nullptr; // DIRECT queue, owned by the device
    ID3D12Fence *_fence = nullptr;      // DIRECT queue fence, owned by the queue
    GstD3D12CmdAllocPool *_ca_pool = nullptr;
    GstD3D12FenceDataPool *_fence_data_pool = nullptr;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _cl;
    std::mutex _mutex;
    uint64_t _last_fence_value = 0;

    uint32_t _src_width, _src_height;
    uint32_t _dst_width, _dst_height;
    DXGI_FORMAT _src_format;

    void SetupPreprocessing(const InputImageLayerDesc::Ptr &pre_proc_info,
                            const ImageTransformationParams::Ptr &image_transform_info, uint32_t src_rect_x,
                            uint32_t src_rect_y, uint32_t src_rect_w, uint32_t src_rect_h);
};

} // namespace InferenceBackend
