/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// ImageToTensorConverter: how each image format and memory type maps to tensor shape, strides and ROI.
// For DMA-BUF images, each pool buffer (identified by its DMA-BUF file descriptor, "fd") is imported into the NPU
// once and the tensor is cached (see ImageRgbpDmabufToNpuTensor).

#include "openvino_image_to_tensor_converter.h"

#include "openvino_core.h"

#include <dlstreamer/openvino/context.h>

#include <openvino/runtime/intel_gpu/properties.hpp>

#ifdef _WIN32
#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>
#endif

#include <cassert>
#include <stdexcept>

namespace InferenceBackend {
namespace OpenVino {

ImageToTensorConverter::ImageToTensorConverter(std::shared_ptr<dlstreamer::OpenVINOContext> gpu_context)
    : gpu_context_(std::move(gpu_context)) {
}

bool ImageToTensorConverter::ImageHasRoi(const Image &image) {
    const auto r = image.rect;
    return r.x || r.y || ((r.width > 0) && (r.width != image.width)) || ((r.height > 0) && (r.height != image.height));
}

std::vector<ov::Tensor> ImageToTensorConverter::ImageToTensors(const Image &image) {
    switch (image.format) {
    case FourCC::FOURCC_RGBP:
    case FourCC::FOURCC_BGRP:
        if (image.type == MemoryType::DMA_BUFFER)
            return {ImageRgbpDmabufToNpuTensor(image)};
        return {ImageRgbpToTensor(image)};

    case FourCC::FOURCC_BGRA:
    case FourCC::FOURCC_BGRX:
    case FourCC::FOURCC_RGBA:
    case FourCC::FOURCC_RGBX:
    case FourCC::FOURCC_BGR:
        if (image.create_cl_buffer)
            return ImageD3dBgrxSurfaceToTensor(image);
        return {ImageBgrxToTensor(image)};

    case FourCC::FOURCC_NV12:
        if (image.type == MemoryType::VAAPI)
            return ImageNv12SurfaceToTensor(image);
        return ImageNv12ToTensor(image);

    case FourCC::FOURCC_I420:
        return ImageI420ToTensor(image);

    default:
        throw std::logic_error("Unsupported image type");
    }
}

ov::Tensor ImageToTensorConverter::ImageRgbpToTensor(const Image &image) {
    // Input image has 3 planes, separately for RGB (or BGR)
    // This function converts 3 planes to ov::Tensor with NCHW layout
    // planes must be equally spaced in memory, and have same stride
    assert(image.planes[0] && image.planes[1] && image.planes[2]);
    assert((image.planes[1] - image.planes[0]) == (image.planes[2] - image.planes[1]));
    assert((image.stride[0] == image.stride[1]) && (image.stride[1] == image.stride[2]));

    auto channels_num = GetChannelsNum(image.format);
    const ov::Shape shape{1, channels_num, size_t(image.height), size_t(image.width)};
    unsigned long plane_stride = image.planes[1] - image.planes[0];
    ov::Strides stride{channels_num * plane_stride, plane_stride, image.stride[0], 1};
    ov::Tensor tensor{ov::element::u8, shape, image.planes[0], stride};

    // ROI
    if (ImageHasRoi(image)) {
        const auto &r = image.rect;
        const ov::Coordinate begin{0, 0, r.y, r.x};
        const ov::Coordinate end{shape[0], shape[1], r.y + r.height, r.x + r.width};
        tensor = ov::Tensor(tensor, begin, end);
    }

    return tensor;
}

// Import DMA-BUF backed RGBP image into NPU as a remote tensor (zero-copy).
// The NPU context is created once and the imported tensor is cached per DMA-BUF fd:
// pool buffers keep stable fds for the element lifetime, and the VA-API VPP overwrites
// the same memory each frame, so a single import per fd can be reused across frames.
ov::Tensor ImageToTensorConverter::ImageRgbpDmabufToNpuTensor(const Image &image) {
    std::lock_guard<std::mutex> lock(npu_dmabuf_cache_mutex_);

    const auto cached = npu_dmabuf_tensor_cache_.find(image.dma_fd);
    if (cached != npu_dmabuf_tensor_cache_.end())
        return cached->second;

    if (!npu_context_)
        npu_context_ = std::make_unique<ov::intel_npu::level_zero::ZeroContext>(OpenVinoCore());

    auto channels_num = GetChannelsNum(image.format);
    // The DMA-BUF rows are padded to a 64-byte aligned pitch (see alignDmaBufPitch). Wrap the whole
    // padded buffer and expose the [1, C, H, W] region as a strided ROI remote tensor.
    const size_t width = image.width;
    const size_t pitch = image.stride[0] ? image.stride[0] : width;
    const ov::Shape padded_shape{1, channels_num, size_t(image.height), pitch};
    ov::RemoteTensor remote_tensor = npu_context_->create_tensor(ov::element::u8, padded_shape, image.dma_fd);
    if (pitch != width) {
        // Crop the padding columns [width, pitch) off every row. The ROI keeps the parent's strides
        // (row stride = pitch), so the NPU must be compiled with NPU_ENABLE_STRIDES_FOR for this input.
        const ov::Coordinate begin{0, 0, 0, 0};
        const ov::Coordinate end{1, channels_num, size_t(image.height), width};
        remote_tensor = ov::RemoteTensor(remote_tensor, begin, end);
    }

    return npu_dmabuf_tensor_cache_.emplace(image.dma_fd, remote_tensor).first->second;
}

ov::Tensor ImageToTensorConverter::ImageBgrxToTensor(const Image &image) {
    // NHWC
    auto channels_num = GetChannelsNum(image.format);
    ov::Shape shape{1, size_t(image.height), size_t(image.width), channels_num};
    ov::Strides stride{size_t(image.height) * image.stride[0], image.stride[0], channels_num, 1};
    ov::Tensor tensor{ov::element::u8, shape, image.planes[0], stride};
    if (ImageHasRoi(image)) {
        const auto &r = image.rect;
        const ov::Coordinate begin{0, r.y, r.x, 0};
        const ov::Coordinate end{shape[0], r.y + r.height, r.x + r.width, shape[3]};
        tensor = ov::Tensor(tensor, begin, end);
    }
    return tensor;
}

std::vector<ov::Tensor> ImageToTensorConverter::ImageNv12ToTensor(const Image &image) {
    // NHWC, 1 for channels as two separate tensors are used
    size_t y_channels_num = 1;
    ov::Shape y_shape{1, size_t(image.height), size_t(image.width), y_channels_num};
    ov::Strides y_stride{size_t(image.height) * image.stride[0], image.stride[0], y_channels_num, 1};
    ov::Tensor y_tensor{ov::element::u8, y_shape, image.planes[0] + image.offsets[0], y_stride};

    // U and V planes are half of Y plane. 2 channels
    size_t uv_channels_num = 2;
    ov::Shape uv_shape{1, size_t(image.height) / 2, size_t(image.width) / 2, uv_channels_num};
    ov::Strides uv_stride{size_t(image.height) / 2 * image.stride[1], image.stride[1], uv_channels_num, 1};
    ov::Tensor uv_tensor{ov::element::u8, uv_shape, image.planes[1] + image.offsets[1], uv_stride};

    // ROI
    if (ImageHasRoi(image)) {
        const auto &r = image.rect;
        ov::Coordinate y_begin{0, r.y, r.x, 0};
        ov::Coordinate y_end{y_shape[0], r.y + r.height, r.x + r.width, 1};
        y_tensor = ov::Tensor(y_tensor, y_begin, y_end);

        ov::Coordinate uv_begin{0, r.y / 2, r.x / 2, 0};
        ov::Coordinate uv_end{uv_shape[0], (r.y + r.height) / 2, (r.x + r.width) / 2, 2};
        uv_tensor = ov::Tensor(uv_tensor, uv_begin, uv_end);
    }

    return {y_tensor, uv_tensor};
}

std::vector<ov::Tensor> ImageToTensorConverter::ImageNv12SurfaceToTensor(const Image &image) {
    auto rmt_ctx = gpu_context_->remote_context();
    auto va_surface = image.va_surface_id;
    auto width = image.width;
    auto height = image.height;

    ov::AnyMap tensor_params = {{ov::intel_gpu::shared_mem_type.name(), "VA_SURFACE"},
                                {ov::intel_gpu::dev_object_handle.name(), va_surface},
                                {ov::intel_gpu::va_plane.name(), uint32_t(0)}};
    auto y_tensor = rmt_ctx.create_tensor(ov::element::u8, {1, height, width, 1}, tensor_params);
    tensor_params[ov::intel_gpu::va_plane.name()] = uint32_t(1);
    auto uv_tensor = rmt_ctx.create_tensor(ov::element::u8, {1, height / 2, width / 2, 2}, tensor_params);

    return {y_tensor, uv_tensor};
}

// D3D11/D3D12 BGRA/RGBA texture wrapped via OpenCL interop by the producer.
// The image's create_cl_buffer callback receives our cl_context and returns a cl_mem holding the texture pixels as
// an u8 NHWC buffer; we wrap that cl_mem as an ov::Tensor on the GPU remote context (D3DContext for D3D11, plain
// ClContext for D3D12, both expose create_tensor(cl_mem)).
std::vector<ov::Tensor> ImageToTensorConverter::ImageD3dBgrxSurfaceToTensor(const Image &image) {
    if (!gpu_context_)
        throw std::runtime_error("D3D surface sharing requires a GPU device with an OpenVINO™ remote context");
    auto cl_rmt_ctx = gpu_context_->remote_context<ov::intel_gpu::ocl::ClContext>();
    auto *cl_ctx = gpu_context_->handle(dlstreamer::OpenVINOContext::key::cl_context);
    auto *cl_buf = static_cast<cl_mem>(image.create_cl_buffer(cl_ctx));
    return {cl_rmt_ctx.create_tensor(ov::element::u8, {1, image.height, image.width, 4}, cl_buf)};
}

std::vector<ov::Tensor> ImageToTensorConverter::ImageI420ToTensor(const Image &image) const {
    assert(image.planes[0] && image.planes[1] && image.planes[2]);
    const ov::Shape y_shape{1, size_t(image.height), size_t(image.width), 1};
    const ov::Shape u_shape{1, size_t(image.height / 2), size_t(image.width / 2), 1};
    const ov::Shape &v_shape{u_shape};

    ov::Tensor y_tensor{ov::element::u8, y_shape, image.planes[0]};
    ov::Tensor u_tensor{ov::element::u8, u_shape, image.planes[1]};
    ov::Tensor v_tensor{ov::element::u8, v_shape, image.planes[2]};

    // ROI
    if (ImageHasRoi(image)) {
        const auto &r = image.rect;
        ov::Coordinate begin{0, r.y, r.x, 0};
        ov::Coordinate end{y_shape[0], r.y + r.height, r.x + r.width, y_shape[3]};

        y_tensor = ov::Tensor(y_tensor, begin, end);

        begin[1] /= 2;
        begin[2] /= 2;
        end[1] /= 2;
        end[2] /= 2;

        u_tensor = ov::Tensor(u_tensor, begin, end);
        v_tensor = ov::Tensor(v_tensor, begin, end);
    }

    return {y_tensor, u_tensor, v_tensor};
}

size_t ImageToTensorConverter::GetChannelsNum(int format) {
    switch (format) {
    case FourCC::FOURCC_BGRA:
    case FourCC::FOURCC_BGRX:
    case FourCC::FOURCC_RGBA:
    case FourCC::FOURCC_RGBX:
        return 4;
    case FourCC::FOURCC_BGR:
    case FourCC::FOURCC_RGBP:
    case FourCC::FOURCC_BGRP:
        return 3;
    }
    return 0;
}

} // namespace OpenVino
} // namespace InferenceBackend
