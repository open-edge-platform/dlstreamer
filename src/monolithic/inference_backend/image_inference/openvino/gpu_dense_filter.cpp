/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gpu_dense_filter.h"

#include "inference_backend/logger.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace InferenceBackend {
namespace OpenVino {

namespace {

// One work-item per (proposal, batch item): best class (lowest id on ties, like cv::minMaxLoc), times objectness if
// any; proposals above the threshold are appended to one list shared by the batch.
// result layout: int count, 7 ints padding, then float8 {x, y, w, h, score, label, proposal, batch} per candidate
const char *kSource = R"CLC(
__kernel void dense_filter(__global const float *out, int per_batch, int n, int sf, int sb, int box_off, int cls_off,
                           int num_cls, int obj_off, float thr, __global int *result, int cap) {
    const int i = get_global_id(0);
    const int b = get_global_id(1);
    if (i >= n)
        return;
    __global const float *p = out + (size_t)b * per_batch;
#define AT(f) p[(size_t)(f) * sf + (size_t)i * sb]
    float best = AT(cls_off);
    int label = 0;
    for (int c = 1; c < num_cls; ++c) {
        const float s = AT(cls_off + c);
        if (s > best) {
            best = s;
            label = c;
        }
    }
    if (obj_off >= 0)
        best *= AT(obj_off);
    if (!(best > thr))
        return;
    const int k = atomic_inc(result);
    if (k >= cap)
        return;
    __global float8 *cand = (__global float8 *)(result + 8);
    cand[k] = (float8)(AT(box_off), AT(box_off + 1), AT(box_off + 2), AT(box_off + 3), best, (float)label, (float)i,
                       (float)b);
#undef AT
}
)CLC";

constexpr size_t kHeaderBytes = 8 * sizeof(cl_int);
constexpr size_t kCapacityPerBatch = 4096;

void Check(cl_int err, const char *what) {
    if (err != CL_SUCCESS)
        throw std::runtime_error(std::string("GPU post-processing: ") + what + " failed, OpenCL error " +
                                 std::to_string(err));
}

// largest float t with (s > t) == (s > threshold) for every float s, so the device compares like the CPU converters,
// which compare the float score against a double threshold
float ThresholdAsFloat(double threshold) {
    float t = static_cast<float>(threshold);
    if (static_cast<double>(t) > threshold)
        t = std::nextafter(t, -INFINITY);
    return t;
}

} // namespace

GpuOutputBuffers::~GpuOutputBuffers() {
    if (candidates && filter)
        filter->mem_free_(filter->context_, candidates);
}

GpuDenseFilter::GpuDenseFilter(cl_context context) : context_(context) {
    Check(clRetainContext(context_), "clRetainContext");
    cl_device_id devices[8];
    size_t bytes = 0;
    Check(clGetContextInfo(context_, CL_CONTEXT_DEVICES, sizeof(devices), devices, &bytes), "clGetContextInfo");
    if (bytes < sizeof(cl_device_id))
        throw std::runtime_error("GPU post-processing: OpenCL context has no device");
    device_ = devices[0];
    cl_int err = CL_SUCCESS;
    queue_ = clCreateCommandQueueWithProperties(context_, device_, nullptr, &err);
    Check(err, "clCreateCommandQueueWithProperties");
    program_ = clCreateProgramWithSource(context_, 1, &kSource, nullptr, &err);
    Check(err, "clCreateProgramWithSource");
    err = clBuildProgram(program_, 1, &device_, "", nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t log_size = 0;
        clGetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::string log(log_size, '\0');
        clGetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
        throw std::runtime_error("GPU post-processing: kernel build failed: " + log);
    }
    kernel_ = clCreateKernel(program_, "dense_filter", &err);
    Check(err, "clCreateKernel");
    cl_platform_id platform = nullptr;
    Check(clGetDeviceInfo(device_, CL_DEVICE_PLATFORM, sizeof(platform), &platform, nullptr), "clGetDeviceInfo");
    host_mem_alloc_ = reinterpret_cast<decltype(host_mem_alloc_)>(
        clGetExtensionFunctionAddressForPlatform(platform, "clHostMemAllocINTEL"));
    mem_free_ =
        reinterpret_cast<decltype(mem_free_)>(clGetExtensionFunctionAddressForPlatform(platform, "clMemFreeINTEL"));
    set_arg_mem_pointer_ = reinterpret_cast<decltype(set_arg_mem_pointer_)>(
        clGetExtensionFunctionAddressForPlatform(platform, "clSetKernelArgMemPointerINTEL"));
    if (!host_mem_alloc_ || !mem_free_ || !set_arg_mem_pointer_)
        throw std::runtime_error("GPU post-processing: cl_intel_unified_shared_memory is not available");
}

GpuDenseFilter::~GpuDenseFilter() {
    if (kernel_)
        clReleaseKernel(kernel_);

    if (program_)
        clReleaseProgram(program_);
    if (queue_)
        clReleaseCommandQueue(queue_);
    if (context_)
        clReleaseContext(context_);
}

std::shared_ptr<GpuOutputBuffers> GpuDenseFilter::CreateBuffers(ov::RemoteTensor tensor, cl_mem output, size_t batch) {
    auto b = std::make_shared<GpuOutputBuffers>();
    b->filter = shared_from_this();
    cl_int err = CL_SUCCESS;
    const size_t output_bytes = tensor.get_byte_size();
    b->tensor = std::move(tensor);
    b->output = output;
    b->capacity = kCapacityPerBatch * std::max<size_t>(batch, 1);
    b->candidates = host_mem_alloc_(context_, nullptr, kHeaderBytes + b->capacity * sizeof(cl_float8), 64, &err);
    Check(err, "clHostMemAllocINTEL(candidates)");
    b->output_bytes = output_bytes;
    b->batch = batch;
    return b;
}

bool GpuDenseFilter::Run(const GpuOutputBuffers &buffers, size_t floats_per_batch, size_t batch,
                         const DenseFilterDesc &d, std::vector<std::vector<DenseCandidate>> &per_batch) {
    const cl_int per_batch_arg = static_cast<cl_int>(floats_per_batch);
    const cl_int cap = static_cast<cl_int>(buffers.capacity);
    const cl_float thr = ThresholdAsFloat(d.threshold);
    const size_t global[2] = {(static_cast<size_t>(d.num_proposals) + 63) / 64 * 64, batch};

    std::lock_guard<std::mutex> lock(mutex_);
    volatile cl_int *result = static_cast<volatile cl_int *>(buffers.candidates);
    result[0] = 0; // the previous use of these buffers has finished (clFinish below)
    int a = 0;
    Check(clSetKernelArg(kernel_, a++, sizeof(cl_mem), &buffers.output), "clSetKernelArg");
    Check(clSetKernelArg(kernel_, a++, sizeof(cl_int), &per_batch_arg), "clSetKernelArg");
    for (int v :
         {d.num_proposals, d.stride_field, d.stride_box, d.box_offset, d.cls_offset, d.num_classes, d.obj_offset})
        Check(clSetKernelArg(kernel_, a++, sizeof(cl_int), &v), "clSetKernelArg");
    Check(clSetKernelArg(kernel_, a++, sizeof(cl_float), &thr), "clSetKernelArg");
    Check(set_arg_mem_pointer_(kernel_, a++, buffers.candidates), "clSetKernelArgMemPointerINTEL");
    Check(clSetKernelArg(kernel_, a++, sizeof(cl_int), &cap), "clSetKernelArg");
    Check(clEnqueueNDRangeKernel(queue_, kernel_, 2, nullptr, global, nullptr, 0, nullptr, nullptr),
          "clEnqueueNDRangeKernel");

    Check(clFinish(queue_), "clFinish");
    const int count = result[0];
    const bool fits = count >= 0 && static_cast<size_t>(count) <= buffers.capacity;
    host_candidates_.clear();
    if (fits && count > 0) {
        const auto *cand =
            reinterpret_cast<const cl_float8 *>(static_cast<const char *>(buffers.candidates) + kHeaderBytes);
        host_candidates_.assign(cand, cand + count);
    }
    if (!fits)
        return false;

    per_batch.assign(batch, {});
    for (int k = 0; k < count; ++k) {
        const cl_float8 &c = host_candidates_[k];
        const size_t b = static_cast<size_t>(c.s[7]);
        if (b >= batch)
            continue;
        per_batch[b].push_back(
            {c.s[0], c.s[1], c.s[2], c.s[3], c.s[4], static_cast<int>(c.s[5]), static_cast<int>(c.s[6])});
    }
    for (auto &v : per_batch)
        std::sort(v.begin(), v.end(),
                  [](const DenseCandidate &x, const DenseCandidate &y) { return x.index < y.index; });
    return true;
}

void GpuDenseFilter::ReadAll(const GpuOutputBuffers &buffers, void *host) {
    std::lock_guard<std::mutex> lock(mutex_);
    Check(clEnqueueReadBuffer(queue_, buffers.output, CL_TRUE, 0, buffers.output_bytes, host, 0, nullptr, nullptr),
          "clEnqueueReadBuffer(output)");
}

OpenvinoDeviceOutputTensor::OpenvinoDeviceOutputTensor(std::shared_ptr<GpuOutputBuffers> buffers, ov::Shape shape,
                                                       ov::element::Type type)
    : buffers_(std::move(buffers)), dims_(shape.begin(), shape.end()) {
    if (type != ov::element::f32)
        throw std::runtime_error("GPU post-processing: only FP32 outputs are supported");
}

const void *OpenvinoDeviceOutputTensor::GetData() const {
    static std::atomic<int> calls{0};
    if (calls++ % 1000 == 0)
        GVA_WARNING("GPU post-processing: output read back to the host (%d times so far)", calls.load());
    if (host_.empty()) {
        host_.resize(buffers_->output_bytes / sizeof(float));
        buffers_->filter->ReadAll(*buffers_, host_.data());
    }
    return host_.data();
}

bool OpenvinoDeviceOutputTensor::FilterDense(const DenseFilterDesc &desc, size_t batch_size,
                                             std::vector<std::vector<DenseCandidate>> &per_batch) const {
    if (batch_size == 0 || batch_size != buffers_->batch) {
        GVA_WARNING("GPU post-processing: batch %zu != output batch %zu", batch_size, buffers_->batch);
        return false;
    }
    const size_t floats = buffers_->output_bytes / sizeof(float);
    return buffers_->filter->Run(*buffers_, floats / batch_size, batch_size, desc, per_batch);
}

} // namespace OpenVino
} // namespace InferenceBackend
