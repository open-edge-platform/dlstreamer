/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <memory>

#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>

struct ID3D11Texture2D;

namespace InferenceBackend {

// D3D11 surface sharing: wrap a D3D11 BGRA/RGBA texture as a GPU cl_mem buffer
// through OpenCL/D3D11 interop (clCreateFromD3D11Texture2DKHR -> cl_image ->
// clEnqueueCopyImageToBuffer -> cl_buf). The image-to-buffer copy is required
// because the OpenVINO GPU plugin tags D3D11-shared cl_images with an internal
// NV12 layout marker that breaks non-NV12 inference.
//
// This class knows only about OpenCL/D3D11, deliberately has no OpenVINO
// dependency. The caller wraps the returned cl_mem as an ov::Tensor via the
// OpenVINO GPU remote context.
class D3D11SurfaceSharing {
  public:
    explicit D3D11SurfaceSharing(cl_context cl_ctx);
    ~D3D11SurfaceSharing();

    D3D11SurfaceSharing(const D3D11SurfaceSharing &) = delete;
    D3D11SurfaceSharing &operator=(const D3D11SurfaceSharing &) = delete;

    // Copy the D3D11 texture's pixels to a GPU cl_mem buffer (u8 NHWC-equivalent
    // layout, size = width * height * 4 bytes) and return the buffer.
    // The returned cl_mem is cached per texture and owned by this object.
    // Callers MUST NOT release the returned cl_mem.
    cl_mem create_cl_buffer(ID3D11Texture2D *texture, uint32_t width, uint32_t height);

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace InferenceBackend
