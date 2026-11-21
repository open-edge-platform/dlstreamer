/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/image.h"

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 300
#endif
#include <CL/cl.h>

#include <array>
#include <functional>
#include <memory>

namespace InferenceBackend {

// The memory objects through which the pre-processing kernel reads one frame, in the OpenCL context of the
// pre-processing. Releases them when destroyed. Shared by the ROIs of the frame and kept until the kernels that read
// it have completed.
struct OpenclSourcePlanes {
    enum class Layout {
        NV12,  // planes: Y (CL_R), UV (CL_RG) images
        I420,  // planes: Y, U, V (CL_R) images
        RGBX,  // planes: one image with 4 channels per pixel
        RGB24, // planes: a buffer, 3 bytes per pixel, rows `stride` bytes apart
    };

    Layout layout = Layout::NV12;
    std::array<cl_mem, 3> planes = {nullptr, nullptr, nullptr};
    cl_uint num_planes = 0;
    uint32_t stride = 0; // RGB24 only
    // For the packed layouts, the order of the channels of a pixel as the kernel reads them (the first three).
    bool bgr_order = false;

    // Called around the kernels when the planes are shared with another API (VA-API, D3D11).
    std::function<void(cl_command_queue)> acquire;
    std::function<void(cl_command_queue)> release;
    // Called when the planes are destroyed, after they are released (if owns_planes), e.g. to destroy a surface they
    // were imported from.
    std::function<void()> on_destroy;
    // If false, on_destroy releases the planes.
    bool owns_planes = true;

    OpenclSourcePlanes() = default;
    OpenclSourcePlanes(const OpenclSourcePlanes &) = delete;
    OpenclSourcePlanes &operator=(const OpenclSourcePlanes &) = delete;
    ~OpenclSourcePlanes();
};

using OpenclSourcePlanesPtr = std::shared_ptr<const OpenclSourcePlanes>;

// Makes frames of one memory type readable by the pre-processing kernels. Called with the pre-processing serialized.
class OpenclFrameSource {
  public:
    virtual ~OpenclFrameSource() = default;

    // The planes of the frame of `image`. Calls for the ROIs of a frame give the same planes. Work needed to read the
    // frame, such as an upload, is enqueued on `queue`.
    virtual OpenclSourcePlanesPtr GetPlanes(const ImagePtr &image, cl_command_queue queue) = 0;
};

// Frames in system memory, uploaded once per frame: NV12, I420, BGRX/BGRA/RGBX/RGBA and BGR/RGB.
std::unique_ptr<OpenclFrameSource> CreateOpenclHostFrameSource(cl_context context);

#ifndef _WIN32
// VA surfaces of `va_display` (NV12), imported through cl_intel_va_api_media_sharing, and NV12 DMA-BUFs, imported into
// a VA surface first. `context` must share surfaces with `va_display`.
std::unique_ptr<OpenclFrameSource> CreateOpenclVaFrameSource(cl_context context, void *va_display);
#else
// Single shared D3D11 textures (NV12, RGBA, BGRA, BGRX), imported through cl_khr_d3d11_sharing. `context` must share
// textures with the D3D11 device of the textures, with CL_CONTEXT_INTEROP_USER_SYNC: the D3D11 work writing a texture
// must have completed when its planes are asked for.
std::unique_ptr<OpenclFrameSource> CreateOpenclD3D11FrameSource(cl_context context, void *gst_d3d11_device);
#endif

} // namespace InferenceBackend
