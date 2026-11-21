/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "opencl_preprocessor.h"

#include "opencl_context.h"
#include "opencl_preproc_geometry.h"
#include "opencl_utils.h"

#include "dlstreamer/base/context.h"
#include "inference_backend/logger.h"
#include "inference_backend/opencl_interop_queue.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace InferenceBackend {

OpenclSourcePlanes::~OpenclSourcePlanes() {
    if (owns_planes)
        for (cl_uint i = 0; i < num_planes; ++i)
            if (planes[i])
                clReleaseMemObject(planes[i]);
    if (on_destroy)
        on_destroy();
}

namespace {

using opencl_preproc::RoiParams;

// Work-group shape: wide along the rows the planes are written in.
constexpr size_t kTileW = 32;
constexpr size_t kTileH = 8;

// cl_intel_unified_shared_memory, for the outputs in host memory. Declared here, not taken from cl_ext.h, which may
// predate the extension.
using HostMemAllocFn = void *(CL_API_CALL *)(cl_context, const cl_ulong *, size_t, cl_uint, cl_int *);
using MemBlockingFreeFn = cl_int(CL_API_CALL *)(cl_context, void *);
using SetKernelArgMemPointerFn = cl_int(CL_API_CALL *)(cl_kernel, cl_uint, const void *);

using Rows = std::array<cl_float4, 3>;

cl_float4 Float4(double x, double y, double z, double w) {
    cl_float4 v;
    v.s[0] = static_cast<float>(x);
    v.s[1] = static_cast<float>(y);
    v.s[2] = static_cast<float>(z);
    v.s[3] = static_cast<float>(w);
    return v;
}

// Rows of the YUV -> RGB conversion of (Y, U, V, 1), normalized, into [0, 255]: BT.601 limited range, with the
// coefficients of OpenVINO™'s NV12/I420 conversion, which the VA surface sharing backend uses.
Rows YuvToRgbRows() {
    auto row = [](double cy, double cu, double cv) {
        return Float4(cy * 255.0, cu * 255.0, cv * 255.0, -(cy * 16.0 + (cu + cv) * 128.0));
    };
    return {row(1.164, 0.0, 1.596),     // R
            row(1.164, -0.391, -0.813), // G
            row(1.164, 2.018, 0.0)};    // B
}

// Rows picking R, G and B from (c0, c1, c2, 1), normalized, the channels in source order, into [0, 255].
Rows RgbRows(bool source_bgr) {
    Rows rows = {};
    for (int c = 0; c < 3; ++c) {
        const int source_channel = source_bgr ? 2 - c : c;
        rows[c] = Float4(source_channel == 0 ? 255.0 : 0.0, source_channel == 1 ? 255.0 : 0.0,
                         source_channel == 2 ? 255.0 : 0.0, 0.0);
    }
    return rows;
}

} // namespace

struct OpenclPreprocessor::Impl {
    Settings settings;
    dlstreamer::ContextPtr app_context;
    bool on_device = false;
    // Outputs in host memory written through device memory: on a discrete GPU the host memory is across PCIe, where
    // the kernels' byte writes are slow (100 ms for a 640x640 input on Arc B60), unlike one transfer.
    bool stage_on_device = false;

    cl_context context = nullptr;
    cl_device_id device = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;
    cl_kernel k_nv12 = nullptr;
    cl_kernel k_i420 = nullptr;
    cl_kernel k_rgbx = nullptr;
    cl_kernel k_rgb24 = nullptr;
    bool fixed_tiles = false;

    HostMemAllocFn host_mem_alloc = nullptr;
    MemBlockingFreeFn mem_free = nullptr;
    SetKernelArgMemPointerFn set_arg_mem_pointer = nullptr;

    Rows yuv_rows = {};

    std::vector<std::unique_ptr<OpenclBatch>> batches;
    std::unique_ptr<OpenclFrameSource> host_source;
    std::unique_ptr<OpenclFrameSource> gpu_source; // VA (Linux) or D3D11 (Windows) frames

    Impl(cl_context device_context, dlstreamer::ContextPtr app_ctx, const Settings &s)
        : settings(s), app_context(std::move(app_ctx)), on_device(device_context != nullptr) {
        if (!settings.width || !settings.height || !settings.batch_size)
            throw std::invalid_argument("OpenCL pre-processing: empty model input");
        if (device_context) {
            opencl::Check(clRetainContext(device_context), "clRetainContext");
            context = device_context;
        } else {
            context = CreateOpenclPreprocContext(app_context);
        }
    }

    // Separate from the constructor, so that the destructor cleans up after a failure half-way.
    void Init() {
        device = opencl::ContextDevice(context);
        const cl_platform_id platform = opencl::DevicePlatform(device);

        cl_int err = CL_SUCCESS;
        // On another engine than the inference where the GPU has one: the CPU waits for the pre-processing of a batch
        // before its inference starts, and should not wait behind the inference work in flight.
        queue = CreateInteropCommandQueue(context, device, &err);
        opencl::Check(err, "clCreateCommandQueue");

        BuildProgram();
        yuv_rows = YuvToRgbRows();

        if (!on_device) {
            host_mem_alloc = opencl::GetExtensionFunction<HostMemAllocFn>(platform, "clHostMemAllocINTEL");
            mem_free = opencl::GetExtensionFunction<MemBlockingFreeFn>(platform, "clMemBlockingFreeINTEL");
            set_arg_mem_pointer =
                opencl::GetExtensionFunction<SetKernelArgMemPointerFn>(platform, "clSetKernelArgMemPointerINTEL");
            if (!host_mem_alloc || !mem_free || !set_arg_mem_pointer)
                throw std::runtime_error("OpenCL pre-processing: cl_intel_unified_shared_memory is not available, "
                                         "needed for inference on a device other than the GPU of the frames");
            cl_bool unified_memory = CL_TRUE;
            clGetDeviceInfo(device, CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(unified_memory), &unified_memory, nullptr);
            stage_on_device = !unified_memory;
        }

        GVA_INFO("OpenCL pre-processing on '%s': %ux%u planar %s, batch %zu, into %s",
                 opencl::DeviceName(device).c_str(), settings.width, settings.height, settings.rgb ? "RGB" : "BGR",
                 settings.batch_size,
                 on_device         ? "device memory of the inference context"
                 : stage_on_device ? "host memory, through device memory"
                                   : "host memory");
    }

    ~Impl() {
        if (queue)
            clFinish(queue);
        // Batches and sources hold planes, which may need the sources' devices to be released.
        for (auto &batch : batches)
            ReleaseBatch(*batch);
        batches.clear();
        host_source.reset();
        gpu_source.reset();
        for (cl_kernel kernel : {k_nv12, k_i420, k_rgbx, k_rgb24})
            if (kernel)
                clReleaseKernel(kernel);
        if (program)
            clReleaseProgram(program);
        if (queue)
            clReleaseCommandQueue(queue);
        if (context)
            clReleaseContext(context);
    }

    void BuildProgram() {
        cl_int err = CL_SUCCESS;
        const char *source = opencl_preproc::KERNELS;
        program = clCreateProgramWithSource(context, 1, &source, nullptr, &err);
        opencl::Check(err, "clCreateProgramWithSource");
        err = clBuildProgram(program, 1, &device, "-cl-fast-relaxed-math", nullptr, nullptr);
        if (err != CL_SUCCESS) {
            size_t size = 0;
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &size);
            std::string log(size, '\0');
            if (size)
                clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, size, &log[0], nullptr);
            GVA_ERROR("OpenCL pre-processing: kernel build failed:\n%s", log.c_str());
            opencl::Check(err, "clBuildProgram");
        }
        auto create = [this](const char *name) {
            cl_int e = CL_SUCCESS;
            cl_kernel kernel = clCreateKernel(program, name, &e);
            opencl::Check(e, "clCreateKernel");
            return kernel;
        };
        k_nv12 = create("pp_nv12");
        k_i420 = create("pp_i420");
        k_rgbx = create("pp_rgbx");
        k_rgb24 = create("pp_rgb24");

        fixed_tiles = true;
        for (cl_kernel kernel : {k_nv12, k_i420, k_rgbx, k_rgb24}) {
            size_t max_items = 0;
            if (clGetKernelWorkGroupInfo(kernel, device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(max_items), &max_items,
                                         nullptr) != CL_SUCCESS ||
                max_items < kTileW * kTileH)
                fixed_tiles = false;
        }
    }

    size_t OutputSize() const {
        return settings.batch_size * 3 * static_cast<size_t>(settings.width) * settings.height;
    }

    OpenclBatch &CreateBatch() {
        auto batch = std::make_unique<OpenclBatch>();
        try {
            cl_int err = CL_SUCCESS;
            if (on_device || stage_on_device) {
                batch->device_memory = clCreateBuffer(context, CL_MEM_READ_WRITE, OutputSize(), nullptr, &err);
                opencl::Check(err, "clCreateBuffer");
            }
            if (!on_device) {
                batch->host_memory = host_mem_alloc(context, nullptr, OutputSize(), 4096, &err);
                opencl::Check(err == CL_SUCCESS && !batch->host_memory ? CL_OUT_OF_HOST_MEMORY : err,
                              "clHostMemAllocINTEL");
            }
            batch->rois.resize(settings.batch_size);
            batch->roi_table =
                clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(RoiParams) * settings.batch_size, nullptr, &err);
            opencl::Check(err, "clCreateBuffer");
        } catch (...) {
            ReleaseBatch(*batch);
            throw;
        }
        batches.push_back(std::move(batch));
        return *batches.back();
    }

    void ReleaseBatch(OpenclBatch &batch) {
        batch.group_planes.reset();
        batch.launched.clear();
        if (batch.roi_table)
            clReleaseMemObject(batch.roi_table);
        if (batch.device_memory)
            clReleaseMemObject(batch.device_memory);
        if (batch.host_memory && mem_free)
            mem_free(context, batch.host_memory);
        batch.roi_table = nullptr;
        batch.device_memory = nullptr;
        batch.host_memory = nullptr;
    }

    OpenclFrameSource &SourceFor(const Image &image) {
        switch (image.type) {
        case MemoryType::SYSTEM:
            if (!host_source)
                host_source = CreateOpenclHostFrameSource(context);
            return *host_source;
#ifndef _WIN32
        case MemoryType::VAAPI:
        case MemoryType::DMA_BUFFER:
            if (!gpu_source) {
                void *display = app_context ? app_context->handle(dlstreamer::BaseContext::key::va_display) : nullptr;
                gpu_source = CreateOpenclVaFrameSource(context, display ? display : image.va_display);
            }
            return *gpu_source;
#else
        case MemoryType::D3D11:
            if (!gpu_source) {
                void *device = app_context ? app_context->handle(dlstreamer::BaseContext::key::d3d11_device) : nullptr;
                gpu_source = CreateOpenclD3D11FrameSource(context, device ? device : image.gst_d3d11_device);
            }
            return *gpu_source;
#endif
        default:
            throw std::invalid_argument("OpenCL pre-processing: unsupported memory type of the frame (" +
                                        std::to_string(static_cast<int>(image.type)) + ")");
        }
    }

    cl_kernel KernelFor(OpenclSourcePlanes::Layout layout) const {
        switch (layout) {
        case OpenclSourcePlanes::Layout::NV12:
            return k_nv12;
        case OpenclSourcePlanes::Layout::I420:
            return k_i420;
        case OpenclSourcePlanes::Layout::RGBX:
            return k_rgbx;
        case OpenclSourcePlanes::Layout::RGB24:
            return k_rgb24;
        }
        throw std::logic_error("OpenCL pre-processing: unknown source layout");
    }

    void Add(OpenclBatch &batch, size_t index, const ImagePtr &image, const InputImageLayerDesc::Ptr &pre_proc_info,
             const ImageTransformationParams::Ptr &image_transform_info) {
        if (!image)
            throw std::invalid_argument("OpenCL pre-processing: null frame");
        if (index >= settings.batch_size || batch.num_rois >= settings.batch_size)
            throw std::out_of_range("OpenCL pre-processing: batch element out of range");

        const OpenclRoiGeometry g = ComputeRoiGeometry(image->width, image->height, image->rect, settings.width,
                                                       settings.height, pre_proc_info, image_transform_info);
        try {
            OpenclSourcePlanesPtr planes = SourceFor(*image).GetPlanes(image, queue);
            if (batch.group_planes && (batch.group_planes != planes || batch.group_frame_width != image->width ||
                                       batch.group_frame_height != image->height))
                Launch(batch);
            if (!batch.group_planes) {
                batch.group_planes = std::move(planes);
                batch.group_first = batch.num_rois;
                batch.group_frame_width = image->width;
                batch.group_frame_height = image->height;
            }
        } catch (...) {
            // An upload may read the frame's memory, which the caller can release once this throws.
            clFinish(queue);
            throw;
        }

        RoiParams &roi = batch.rois[batch.num_rois];
        roi = RoiParams();
        roi.dst_rect[0] = g.dst_x;
        roi.dst_rect[1] = g.dst_y;
        roi.dst_rect[2] = g.dst_w;
        roi.dst_rect[3] = g.dst_h;
        roi.src_map[0] = static_cast<float>(g.src_x);
        roi.src_map[1] = static_cast<float>(g.src_y);
        roi.src_map[2] = static_cast<float>(g.src_w / g.dst_w);
        roi.src_map[3] = static_cast<float>(g.src_h / g.dst_h);
        for (int c = 0; c < 3; ++c)
            roi.fill[c] = g.fill_rgb[settings.rgb ? c : 2 - c];
        roi.index = static_cast<uint32_t>(index);
        ++batch.num_rois;
    }

    void SetDstArg(cl_kernel kernel, cl_uint arg, const OpenclBatch &batch) {
        if (batch.device_memory)
            opencl::Check(clSetKernelArg(kernel, arg, sizeof(cl_mem), &batch.device_memory), "clSetKernelArg");
        else
            opencl::Check(set_arg_mem_pointer(kernel, arg, batch.host_memory), "clSetKernelArgMemPointerINTEL");
    }

    // Enqueues the kernel for the entries of the batch not launched yet, all of one frame.
    void Launch(OpenclBatch &batch) {
        const size_t count = batch.num_rois - batch.group_first;
        if (!batch.group_planes || count == 0) {
            batch.group_planes.reset();
            batch.group_first = batch.num_rois;
            return;
        }
        const OpenclSourcePlanes &planes = *batch.group_planes;
        const cl_kernel kernel = KernelFor(planes.layout);
        const float w = static_cast<float>(batch.group_frame_width);
        const float h = static_cast<float>(batch.group_frame_height);

        opencl::Check(clEnqueueWriteBuffer(queue, batch.roi_table, CL_FALSE, batch.group_first * sizeof(RoiParams),
                                           count * sizeof(RoiParams), &batch.rois[batch.group_first], 0, nullptr,
                                           nullptr),
                      "clEnqueueWriteBuffer");

        cl_uint arg = 0;
        cl_int err = CL_SUCCESS;
        if (planes.layout == OpenclSourcePlanes::Layout::RGB24) {
            const cl_int stride = static_cast<cl_int>(planes.stride);
            const cl_int src_w = static_cast<cl_int>(batch.group_frame_width);
            const cl_int src_h = static_cast<cl_int>(batch.group_frame_height);
            err |= clSetKernelArg(kernel, arg++, sizeof(cl_mem), &planes.planes[0]);
            err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &stride);
            err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &src_w);
            err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &src_h);
        } else {
            for (cl_uint i = 0; i < planes.num_planes; ++i)
                err |= clSetKernelArg(kernel, arg++, sizeof(cl_mem), &planes.planes[i]);
        }
        const cl_uint roi_offset = static_cast<cl_uint>(batch.group_first);
        const cl_int dst_w = static_cast<cl_int>(settings.width);
        const cl_int dst_h = static_cast<cl_int>(settings.height);
        const cl_float4 clamp0 = Float4(0.5, 0.5, w - 0.5, h - 0.5);
        const cl_float4 clamp1 =
            Float4(0.5, 0.5, (batch.group_frame_width + 1) / 2 - 0.5, (batch.group_frame_height + 1) / 2 - 0.5);
        const bool yuv =
            planes.layout == OpenclSourcePlanes::Layout::NV12 || planes.layout == OpenclSourcePlanes::Layout::I420;
        const Rows rgb_rows = yuv ? yuv_rows : RgbRows(planes.bgr_order);
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_mem), &batch.roi_table);
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_uint), &roi_offset);
        opencl::Check(err, "clSetKernelArg");
        SetDstArg(kernel, arg++, batch);
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &dst_w);
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_int), &dst_h);
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_float4), &clamp0);
        err |= clSetKernelArg(kernel, arg++, sizeof(cl_float4), &clamp1);
        // Rows in the model's channel order.
        for (int c = 0; c < 3; ++c)
            err |= clSetKernelArg(kernel, arg++, sizeof(cl_float4), &rgb_rows[settings.rgb ? c : 2 - c]);
        opencl::Check(err, "clSetKernelArg");

        const size_t tile[3] = {kTileW, kTileH, 1};
        const size_t global[3] = {fixed_tiles ? (settings.width + kTileW - 1) / kTileW * kTileW : settings.width,
                                  fixed_tiles ? (settings.height + kTileH - 1) / kTileH * kTileH : settings.height,
                                  count};
        if (planes.acquire)
            planes.acquire(queue);
        const cl_int enqueued = clEnqueueNDRangeKernel(queue, kernel, 3, nullptr, global, fixed_tiles ? tile : nullptr,
                                                       0, nullptr, nullptr);
        // Released even if the kernel was not enqueued: an object left acquired fails every later acquire.
        if (planes.release)
            planes.release(queue);
        opencl::Check(enqueued, "clEnqueueNDRangeKernel");

        batch.launched.push_back(std::move(batch.group_planes));
        batch.group_planes.reset();
        batch.group_first = batch.num_rois;
    }

    // Copies the batch elements written so far from the device memory to the host memory.
    void CopyToHost(const OpenclBatch &batch) {
        size_t elements = 0;
        for (size_t i = 0; i < batch.num_rois; ++i)
            elements = (std::max)(elements, static_cast<size_t>(batch.rois[i].index) + 1);
        if (elements == 0)
            return;
        const size_t element_size = 3 * static_cast<size_t>(settings.width) * settings.height;
        opencl::Check(clEnqueueReadBuffer(queue, batch.device_memory, CL_FALSE, 0, elements * element_size,
                                          batch.host_memory, 0, nullptr, nullptr),
                      "clEnqueueReadBuffer");
    }

    void Finish(OpenclBatch &batch) {
        auto reset = [&batch]() {
            batch.group_planes.reset();
            batch.launched.clear();
            batch.num_rois = 0;
            batch.group_first = 0;
        };
        try {
            Launch(batch);
            if (!on_device && batch.device_memory)
                CopyToHost(batch);
            opencl::Check(clFinish(queue), "clFinish");
        } catch (...) {
            clFinish(queue);
            reset();
            throw;
        }
        reset();
    }
};

OpenclPreprocessor::OpenclPreprocessor(cl_context device_context, dlstreamer::ContextPtr app_context,
                                       const Settings &settings)
    : impl_(std::make_unique<Impl>(device_context, std::move(app_context), settings)) {
    impl_->Init();
}

OpenclPreprocessor::~OpenclPreprocessor() = default;

bool OpenclPreprocessor::OutputsOnDevice() const {
    return impl_->on_device;
}

const OpenclPreprocessor::Settings &OpenclPreprocessor::GetSettings() const {
    return impl_->settings;
}

OpenclBatch &OpenclPreprocessor::CreateBatch() {
    return impl_->CreateBatch();
}

void OpenclPreprocessor::Add(OpenclBatch &batch, size_t index, const ImagePtr &image,
                             const InputImageLayerDesc::Ptr &pre_proc_info,
                             const ImageTransformationParams::Ptr &image_transform_info) {
    impl_->Add(batch, index, image, pre_proc_info, image_transform_info);
}

void OpenclPreprocessor::Finish(OpenclBatch &batch) {
    impl_->Finish(batch);
}

} // namespace InferenceBackend
