/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Wraps the memory of an InferenceBackend::Image as ov::Tensor(s) without copying it, so it can be set as model input.
// Covers the formats the backend feeds to a model (RGBP/BGRP, BGRX family, NV12, I420) and the two zero-copy paths:
// DMA-BUF imported into the NPU, and VA surfaces shared with the GPU.

#pragma once

#include "inference_backend/image.h"

#include <dlstreamer/openvino/context.h>

#include <openvino/openvino.hpp>
#include <openvino/runtime/intel_npu/level_zero/level_zero.hpp>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace InferenceBackend {
namespace OpenVino {

class ImageToTensorConverter {
  public:
    // gpu_context is only used for images that are VA surfaces (NV12 in GPU memory); it may be null otherwise.
    explicit ImageToTensorConverter(dlstreamer::OpenVINOContextPtr gpu_context);

    // One tensor per model input plane: 1 for packed and planar RGB, 2 for NV12 (Y, UV), 3 for I420 (Y, U, V).
    // The tensors reference the image memory, so the image must outlive them.
    std::vector<ov::Tensor> image_to_tensors(const Image &image);

  private:
    static bool image_has_roi(const Image &image);
    static size_t get_channels_num(int format);

    ov::Tensor image_rgbp_to_tensor(const Image &image);
    ov::Tensor image_rgbp_dmabuf_to_npu_tensor(const Image &image);
    ov::Tensor image_bgrx_to_tensor(const Image &image);
    std::vector<ov::Tensor> image_nv12_to_tensor(const Image &image);
    std::vector<ov::Tensor> image_nv12_surface_to_tensor(const Image &image);
    std::vector<ov::Tensor> image_i420_to_tensor(const Image &image) const;

    dlstreamer::OpenVINOContextPtr _gpu_context;

    // Persistent NPU Level Zero context and per-DMA-BUF-fd remote tensor cache for the
    // VAAPI_SYSTEM zero-copy path (avoids re-creating a context and re-importing the fd
    // on every inference, which leaks NPU imports and stalls inference over time).
    std::unique_ptr<ov::intel_npu::level_zero::ZeroContext> _npu_context;
    std::unordered_map<int, ov::Tensor> _npu_dmabuf_tensor_cache;
    std::mutex _npu_dmabuf_cache_mutex;
};

} // namespace OpenVino
} // namespace InferenceBackend
