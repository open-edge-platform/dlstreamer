/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// PROTOTYPE (gpu_post_process): post-processing on the GPU for dense "proposals x fields" outputs (yolo v8/v11 ...).
// The output tensor of an infer request is an OpenCL buffer owned here and handed to OpenVINO, so the result stays in
// device memory; DenseFilter reduces it to the proposals above the threshold and only those are read back.

#pragma once

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 300
#endif
#include <CL/cl.h>

#include "inference_backend/image_inference.h"

#include <openvino/openvino.hpp>

#include <memory>
#include <mutex>
#include <vector>

namespace InferenceBackend {
namespace OpenVino {

class GpuDenseFilter;

// One output of one infer request: the device buffer OpenVINO writes into and the reduction results
struct GpuOutputBuffers {
    std::shared_ptr<GpuDenseFilter> filter;
    // allocated by OpenVINO: a buffer created by the caller is not used as the network output, OpenVINO writes its
    // own buffer and copies (bcs stays as busy as without device outputs)
    ov::RemoteTensor tensor;
    cl_mem output = nullptr; // of tensor, not owned
    // USM host memory (cl_intel_unified_shared_memory): int count + padding, then float8 {x, y, w, h, score, label,
    // proposal, batch}. The kernel writes it over PCIe and the host reads it in place: no map, no copy command
    void *candidates = nullptr;
    size_t output_bytes = 0;
    size_t capacity = 0;        // candidates of the whole batch
    size_t batch = 0;
    ~GpuOutputBuffers();
};

class GpuDenseFilter : public std::enable_shared_from_this<GpuDenseFilter> {
  public:
    explicit GpuDenseFilter(cl_context context);
    ~GpuDenseFilter();

    std::shared_ptr<GpuOutputBuffers> CreateBuffers(ov::RemoteTensor tensor, cl_mem output, size_t batch);
    // false when a batch item has more candidates than the buffer holds
    bool Run(const GpuOutputBuffers &buffers, size_t floats_per_batch, size_t batch, const DenseFilterDesc &desc,
             std::vector<std::vector<DenseCandidate>> &per_batch);
    void ReadAll(const GpuOutputBuffers &buffers, void *host);
    cl_context Context() const {
        return context_;
    }

  private:
    cl_context context_ = nullptr;
    cl_device_id device_ = nullptr;
    cl_command_queue queue_ = nullptr;
    cl_program program_ = nullptr;
    cl_kernel kernel_ = nullptr;
    void *(*host_mem_alloc_)(cl_context, const cl_ulong *, size_t, cl_uint, cl_int *) = nullptr;
    cl_int (*mem_free_)(cl_context, void *) = nullptr;
    cl_int (*set_arg_mem_pointer_)(cl_kernel, cl_uint, const void *) = nullptr;
    friend struct GpuOutputBuffers;
    std::mutex mutex_; // kernel arguments and the queue are shared by the completion callbacks
    std::vector<cl_float8> host_candidates_;
};

// OutputBlob over a device buffer: FilterDense runs on the GPU, GetData() reads the whole tensor back (fallback for
// converters without a GPU path)
class OpenvinoDeviceOutputTensor : public OutputBlob {
  public:
    OpenvinoDeviceOutputTensor(std::shared_ptr<GpuOutputBuffers> buffers, ov::Shape shape, ov::element::Type type);
    const std::vector<size_t> &GetDims() const override {
        return dims_;
    }
    Layout GetLayout() const override {
        return Layout::ANY;
    }
    Precision GetPrecision() const override {
        return Precision::FP32;
    }
    const void *GetData() const override;
    bool FilterDense(const DenseFilterDesc &desc, size_t batch_size,
                     std::vector<std::vector<DenseCandidate>> &per_batch) const override;

  private:
    std::shared_ptr<GpuOutputBuffers> buffers_;
    std::vector<size_t> dims_;
    mutable std::vector<float> host_;
};

} // namespace OpenVino
} // namespace InferenceBackend
