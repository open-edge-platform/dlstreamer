/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/d3d11/gstd3d11.h>

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 300
#endif
#include <CL/cl.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace InferenceBackend {

// Which part of the frame goes where in the model input tensor.
struct OpenCLPreprocGeometry {
    // Source rectangle, in frame pixels.
    double src_x = 0, src_y = 0, src_w = 0, src_h = 0;
    // Destination rectangle, in tensor pixels. The rest of the tensor is padding.
    int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;
    // Padding colour as R, G, B.
    uint8_t fill_rgb[3] = {0, 0, 0};
};

// Output tensors of one OpenCL context, recycled. Shared with the images that hold a buffer, so that
// those can outlive the preprocessor.
class OpenCLBufferPool {
  public:
    OpenCLBufferPool(cl_context context, size_t size);
    ~OpenCLBufferPool();

    OpenCLBufferPool(const OpenCLBufferPool &) = delete;
    OpenCLBufferPool &operator=(const OpenCLBufferPool &) = delete;

    cl_mem acquire();
    void release(cl_mem buffer);

  private:
    cl_context _context;
    size_t _size;
    std::mutex _mutex;
    std::vector<cl_mem> _free;
};

// Pre-processes frames held in shared D3D11 textures (NV12, RGBA or BGRA) into the model input, an u8
// planar (NCHW, three channels) tensor, with a single OpenCL kernel: colour conversion, crop, resize
// and padding. Bound to the OpenCL context of OpenVINO's GPU remote context, which shares textures
// with the D3D11 device, so its output buffers go to OpenVINO as they are.
class OpenCLPreproc {
  public:
    OpenCLPreproc(cl_context context, GstD3D11Device *device, uint32_t dst_width, uint32_t dst_height, bool bgr);
    ~OpenCLPreproc();

    OpenCLPreproc(const OpenCLPreproc &) = delete;
    OpenCLPreproc &operator=(const OpenCLPreproc &) = delete;

    cl_context context() const;

    const std::shared_ptr<OpenCLBufferPool> &output_pool() const;

    // Pre-processes the frame in `texture`, a single shared texture of the device of which the top
    // left `frame_width` x `frame_height` are the frame, into `output`. The D3D11 work that filled
    // the texture must have completed: the OpenCL context leaves D3D11 sync to the caller
    // (CL_CONTEXT_INTEROP_USER_SYNC). Returns once the kernel has finished.
    void convert(ID3D11Texture2D *texture, uint32_t frame_width, uint32_t frame_height,
                 const OpenCLPreprocGeometry &geometry, cl_mem output);

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace InferenceBackend
