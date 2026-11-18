/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/image.h"
#include "inference_backend/input_image_layer_descriptor.h"

#include <va/va.h>

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 300
#endif
#include <CL/cl.h>
#include <CL/cl_va_api_media_sharing_intel.h>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace InferenceBackend {

// Crops and scales the ROIs of decoded NV12 VA surfaces with an OpenCL kernel, straight into a batched u8 NHWC BGR or
// RGB buffer that OpenVINO™ reads as the model input. It replaces the per-ROI VPP call of the va-surface-sharing
// backend: on small ROIs that call blocks in vaEndPicture for ~0.4 ms, which capped pipelines with several models on
// the same ROIs (issue #1259). One kernel launch per ROI, no VA surface pool, no host wait until the batch is full.
//
// The crop, resize, aspect-ratio and padding regions are the ones VaApiConverter computes for VPP, so model-proc
// pre-processing parameters keep their meaning and ImageTransformationParams are filled the same way.
//
// Runs on `context`, the OpenCL context of the OpenVINO™ GPU plugin, created with the VA display of the frames.
// Not thread-safe: the caller serializes all calls.
class VaRoiGpuPreproc {
  public:
    VaRoiGpuPreproc(cl_context context, VADisplay display, uint32_t dst_width, uint32_t dst_height, bool rgb);
    ~VaRoiGpuPreproc();

    VaRoiGpuPreproc(const VaRoiGpuPreproc &) = delete;
    VaRoiGpuPreproc &operator=(const VaRoiGpuPreproc &) = delete;

    // A buffer for `batch` model inputs, [batch, dst_height, dst_width, 3] u8, owned by this object.
    cl_mem CreateBatchBuffer(size_t batch);

    // Enqueues the crop and scale of src.rect (the whole frame if the rect is empty) of the NV12 surface `src` into
    // slot `index` of `dst`, a buffer from CreateBatchBuffer(). Returns without waiting for the GPU.
    void Enqueue(const Image &src, cl_mem dst, size_t index, const InputImageLayerDesc::Ptr &pre_proc_info,
                 const ImageTransformationParams::Ptr &image_transform_info);

    // Blocks until all enqueued work is done, so that OpenVINO™ can read the buffers.
    void Finish();

  private:
    using Planes = std::array<cl_mem, 2>;
    Planes GetPlanes(VASurfaceID surface);
    void ReleaseImports();
    void Cleanup();

    cl_context _context = nullptr;
    cl_device_id _device = nullptr;
    cl_command_queue _queue = nullptr;
    cl_program _program = nullptr;
    cl_kernel _kernel = nullptr;
    VADisplay _display = nullptr;
    uint32_t _dst_width = 0;
    uint32_t _dst_height = 0;
    bool _rgb = false;

    clCreateFromVA_APIMediaSurfaceINTEL_fn _create_from_va = nullptr;
    clEnqueueAcquireVA_APIMediaSurfacesINTEL_fn _acquire = nullptr;
    clEnqueueReleaseVA_APIMediaSurfacesINTEL_fn _release = nullptr;

    // Imports of the decoder's surfaces, kept for the lifetime of this object, as the decoder pool is fixed. On i915
    // an import must not outlive its frame (it keeps the surface attached to the GPU context and breaks CPU maps of
    // that surface elsewhere), so there only the current frame's import is kept.
    bool _cache_imports = true;
    std::unordered_map<VASurfaceID, Planes> _imports;

    std::vector<cl_mem> _batch_buffers;
};

} // namespace InferenceBackend
