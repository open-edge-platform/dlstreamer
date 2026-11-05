/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <cstdint>
#include <memory>

#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>

#include <gst/d3d12/gstd3d12.h>

namespace InferenceBackend {

// D3D12 surface sharing: BGRA/RGBA D3D12 texture -> shared D3D12 buffer
// (CopyTextureRegion on the COPY queue) -> OpenCL buffer imported through
// cl_khr_external_memory (OPAQUE_WIN32 NT handle) -> copy into a regular buffer ->
// packed u8 NHWC cl_mem for OpenVINO. OpenVINO reads after the external buffer is
// released, which cl_khr_external_memory leaves undefined, hence the copy.
//
// Unlike the D3D11 path the texture itself is never seen by OpenCL; only a linear
// buffer is shared. The per-texture resources (shared buffer, NT handle, CL
// imports, command list) are cached because the producer recycles a small
// texture pool.
//
// This class knows only about OpenCL and D3D12, no OpenVINO dependency.
class D3D12SurfaceSharing {
  public:
    explicit D3D12SurfaceSharing(cl_context cl_ctx);
    ~D3D12SurfaceSharing();

    D3D12SurfaceSharing(const D3D12SurfaceSharing &) = delete;
    D3D12SurfaceSharing &operator=(const D3D12SurfaceSharing &) = delete;

    // Copy `texture` (subresource `subresource`, written by `device`'s DIRECT queue up to
    // `wait_fence`/`wait_value`) into a GPU cl_mem buffer laid out as packed
    // width*height*4 bytes and return it. The returned cl_mem is cached per texture
    // and owned by this object; callers MUST NOT release it.
    cl_mem create_cl_buffer(GstD3D12Device *device, ID3D12Resource *texture, uint32_t subresource, uint32_t width,
                            uint32_t height, ID3D12Fence *wait_fence, uint64_t wait_value);

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace InferenceBackend
