/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "va_roi_gpu_preproc.h"

#include "inference_backend/logger.h"
#include "vaapi_converter.h"
#include "vaapi_utils.h"

#include <va/va_drmcommon.h>

#include <climits>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace InferenceBackend {

namespace {

// One work-item per output pixel. Pixels inside the destination region are sampled bilinearly from the source region
// of the NV12 surface (Y and UV planes separately, as VPP scales the planes before OpenVINO™ converts the color), then
// converted with the BT.601 limited-range coefficients of OpenVINO™'s NV12 color conversion. Pixels outside the region
// (aspect-ratio and model-proc padding) get the background color, as VPP fills them.
constexpr const char *kKernelSource = R"CLC(
__constant sampler_t kSampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_LINEAR;

__kernel void nv12_roi_to_packed(__read_only image2d_t y_plane, __read_only image2d_t uv_plane,
                                 __global uchar *dst, ulong dst_offset, int dst_w, int dst_h,
                                 float src_x, float src_y, float scale_x, float scale_y,
                                 int reg_x, int reg_y, int reg_w, int reg_h,
                                 float max_x, float max_y, uchar4 fill, int rgb) {
    const int x = get_global_id(0);
    const int y = get_global_id(1);
    if (x >= dst_w || y >= dst_h)
        return;

    float r = fill.x, g = fill.y, b = fill.z;
    if (x >= reg_x && y >= reg_y && x < reg_x + reg_w && y < reg_y + reg_h) {
        // Pixel centers: destination pixel (x, y) maps to this source position, in luma pixels.
        const float sx = clamp(src_x + ((float)(x - reg_x) + 0.5f) * scale_x, 0.5f, max_x);
        const float sy = clamp(src_y + ((float)(y - reg_y) + 0.5f) * scale_y, 0.5f, max_y);
        const float luma = read_imagef(y_plane, kSampler, (float2)(sx, sy)).x * 255.0f;
        const float cx = clamp(sx * 0.5f, 0.5f, max_x * 0.5f - 0.25f);
        const float cy = clamp(sy * 0.5f, 0.5f, max_y * 0.5f - 0.25f);
        const float2 uv = read_imagef(uv_plane, kSampler, (float2)(cx, cy)).xy * 255.0f;
        const float c = luma - 16.0f, d = uv.x - 128.0f, e = uv.y - 128.0f;
        r = 1.164f * c + 1.596f * e;
        g = 1.164f * c - 0.391f * d - 0.813f * e;
        b = 1.164f * c + 2.018f * d;
    }

    __global uchar *p = dst + dst_offset + ((size_t)y * dst_w + x) * 3;
    const uchar R = convert_uchar_sat_rte(r), G = convert_uchar_sat_rte(g), B = convert_uchar_sat_rte(b);
    if (rgb) {
        p[0] = R; p[1] = G; p[2] = B;
    } else {
        p[0] = B; p[1] = G; p[2] = R;
    }
}
)CLC";

void ClCheck(cl_int err, const char *what) {
    if (err != CL_SUCCESS)
        throw std::runtime_error(std::string("VaRoiGpuPreproc: ") + what + " failed, OpenCL error " +
                                 std::to_string(err));
}

// Kernel driver of the DRM device behind the VA display ("i915", "xe", ...), empty if unknown.
std::string DrmDriverName(VADisplay display) {
    try {
        auto *drm = static_cast<drm_state *>(VaDpyWrapper::fromHandle(display).drvCtx()->drm_state);
        struct stat st = {};
        if (!drm || drm->fd < 0 || fstat(drm->fd, &st) != 0 || !S_ISCHR(st.st_mode))
            return {};
        const std::string link = "/sys/dev/char/" + std::to_string(major(st.st_rdev)) + ":" +
                                 std::to_string(minor(st.st_rdev)) + "/device/driver";
        char target[PATH_MAX] = {};
        const ssize_t n = readlink(link.c_str(), target, sizeof(target) - 1);
        if (n <= 0)
            return {};
        const std::string path(target, n);
        return path.substr(path.find_last_of('/') + 1);
    } catch (const std::exception &) {
        return {};
    }
}

} // namespace

VaRoiGpuPreproc::VaRoiGpuPreproc(cl_context context, VADisplay display, uint32_t dst_width, uint32_t dst_height,
                                 bool rgb)
    : _display(display), _dst_width(dst_width), _dst_height(dst_height), _rgb(rgb) {
    if (!context || !_display || !_dst_width || !_dst_height)
        throw std::invalid_argument("VaRoiGpuPreproc: invalid OpenCL context, VA display or output size");

    try {
        ClCheck(clRetainContext(context), "clRetainContext");
        _context = context;

        cl_device_id devices[8] = {};
        size_t size = 0;
        ClCheck(clGetContextInfo(_context, CL_CONTEXT_DEVICES, sizeof(devices), devices, &size), "clGetContextInfo");
        if (size < sizeof(cl_device_id))
            throw std::runtime_error("VaRoiGpuPreproc: the OpenCL context has no device");
        _device = devices[0];

        cl_platform_id platform = nullptr;
        ClCheck(clGetDeviceInfo(_device, CL_DEVICE_PLATFORM, sizeof(platform), &platform, nullptr), "clGetDeviceInfo");
        _create_from_va = reinterpret_cast<clCreateFromVA_APIMediaSurfaceINTEL_fn>(
            clGetExtensionFunctionAddressForPlatform(platform, "clCreateFromVA_APIMediaSurfaceINTEL"));
        _acquire = reinterpret_cast<clEnqueueAcquireVA_APIMediaSurfacesINTEL_fn>(
            clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueAcquireVA_APIMediaSurfacesINTEL"));
        _release = reinterpret_cast<clEnqueueReleaseVA_APIMediaSurfacesINTEL_fn>(
            clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueReleaseVA_APIMediaSurfacesINTEL"));
        if (!_create_from_va || !_acquire || !_release)
            throw std::runtime_error("VaRoiGpuPreproc: cl_intel_va_api_media_sharing is not available");

        cl_int err = CL_SUCCESS;
        _queue = clCreateCommandQueueWithProperties(_context, _device, nullptr, &err);
        ClCheck(err, "clCreateCommandQueueWithProperties");

        const char *source = kKernelSource;
        _program = clCreateProgramWithSource(_context, 1, &source, nullptr, &err);
        ClCheck(err, "clCreateProgramWithSource");
        err = clBuildProgram(_program, 1, &_device, "", nullptr, nullptr);
        if (err != CL_SUCCESS) {
            char log[4096] = {};
            clGetProgramBuildInfo(_program, _device, CL_PROGRAM_BUILD_LOG, sizeof(log) - 1, log, nullptr);
            GVA_ERROR("VaRoiGpuPreproc: kernel build log:\n%s", log);
            ClCheck(err, "clBuildProgram");
        }
        _kernel = clCreateKernel(_program, "nv12_roi_to_packed", &err);
        ClCheck(err, "clCreateKernel");
    } catch (...) {
        Cleanup();
        throw;
    }

    const std::string driver = DrmDriverName(_display);
    _cache_imports = driver != "i915";
    GVA_INFO("VaRoiGpuPreproc: GPU ROI pre-processing to %ux%u %s, driver '%s', surface imports %s", _dst_width,
             _dst_height, _rgb ? "RGB" : "BGR", driver.c_str(), _cache_imports ? "cached" : "per frame");
}

VaRoiGpuPreproc::~VaRoiGpuPreproc() {
    Cleanup();
}

void VaRoiGpuPreproc::Cleanup() {
    if (_queue)
        clFinish(_queue);
    ReleaseImports();
    for (cl_mem buffer : _batch_buffers)
        clReleaseMemObject(buffer);
    _batch_buffers.clear();
    if (_kernel)
        clReleaseKernel(_kernel);
    if (_program)
        clReleaseProgram(_program);
    if (_queue)
        clReleaseCommandQueue(_queue);
    if (_context)
        clReleaseContext(_context);
    _kernel = nullptr;
    _program = nullptr;
    _queue = nullptr;
    _context = nullptr;
}

cl_mem VaRoiGpuPreproc::CreateBatchBuffer(size_t batch) {
    cl_int err = CL_SUCCESS;
    const size_t size = batch * _dst_width * _dst_height * 3;
    cl_mem buffer = clCreateBuffer(_context, CL_MEM_READ_WRITE, size, nullptr, &err);
    ClCheck(err, "clCreateBuffer");
    _batch_buffers.push_back(buffer);
    return buffer;
}

void VaRoiGpuPreproc::ReleaseImports() {
    for (auto &entry : _imports)
        for (cl_mem plane : entry.second)
            if (plane)
                clReleaseMemObject(plane);
    _imports.clear();
}

VaRoiGpuPreproc::Planes VaRoiGpuPreproc::GetPlanes(VASurfaceID surface) {
    if (auto it = _imports.find(surface); it != _imports.end())
        return it->second;

    // An import must not outlive its frame: drop the others. Enqueued kernels keep using them until they complete.
    if (!_cache_imports)
        ReleaseImports();

    Planes planes = {nullptr, nullptr};
    for (cl_uint plane = 0; plane < 2; ++plane) {
        cl_int err = CL_SUCCESS;
        VASurfaceID id = surface;
        planes[plane] = _create_from_va(_context, CL_MEM_READ_ONLY, &id, plane, &err);
        if (err != CL_SUCCESS || !planes[plane]) {
            if (planes[0])
                clReleaseMemObject(planes[0]);
            ClCheck(err != CL_SUCCESS ? err : CL_INVALID_MEM_OBJECT, "clCreateFromVA_APIMediaSurfaceINTEL");
        }
    }
    return _imports.emplace(surface, planes).first->second;
}

void VaRoiGpuPreproc::Enqueue(const Image &src, cl_mem dst, size_t index, const InputImageLayerDesc::Ptr &pre_proc_info,
                              const ImageTransformationParams::Ptr &image_transform_info) {
    if (src.type != MemoryType::VAAPI || src.format != FourCC::FOURCC_NV12)
        throw std::invalid_argument("VaRoiGpuPreproc: only NV12 VA surfaces are supported");
    if (src.va_display != _display)
        throw std::invalid_argument("VaRoiGpuPreproc: the frame comes from another VA display than the inference");

    // Same regions as the VPP path (VaApiConverter::Convert).
    VARectangle src_region = {static_cast<int16_t>(src.rect.x), static_cast<int16_t>(src.rect.y),
                              static_cast<uint16_t>(src.rect.width), static_cast<uint16_t>(src.rect.height)};
    if (src_region.width == 0 || src_region.height == 0)
        src_region = {0, 0, static_cast<uint16_t>(src.width), static_cast<uint16_t>(src.height)};
    VARectangle dst_region = {0, 0, src_region.width, src_region.height};
    uint32_t background = 0xff000000;
    if (pre_proc_info && pre_proc_info->isDefined()) {
        VAProcPipelineParameterBuffer pipeline_param;
        std::memset(&pipeline_param, 0, sizeof(pipeline_param));
        pipeline_param.output_background_color = background;
        VaApiConverter::SetupPipelineRegionsWithCustomParams(
            pre_proc_info, static_cast<uint16_t>(src.width), static_cast<uint16_t>(src.height),
            static_cast<uint16_t>(_dst_width), static_cast<uint16_t>(_dst_height), src_region, dst_region,
            pipeline_param, image_transform_info);
        background = pipeline_param.output_background_color;
    } else {
        dst_region = {0, 0, static_cast<uint16_t>(_dst_width), static_cast<uint16_t>(_dst_height)};
    }
    if (dst_region.width == 0 || dst_region.height == 0 || src_region.width == 0 || src_region.height == 0)
        throw std::runtime_error("VaRoiGpuPreproc: empty source or destination region");

    const Planes planes = GetPlanes(src.va_surface_id);

    const cl_ulong dst_offset = static_cast<cl_ulong>(index) * _dst_width * _dst_height * 3;
    const cl_int dst_w = static_cast<cl_int>(_dst_width), dst_h = static_cast<cl_int>(_dst_height);
    const cl_float src_x = src_region.x, src_y = src_region.y;
    const cl_float scale_x = static_cast<float>(src_region.width) / dst_region.width;
    const cl_float scale_y = static_cast<float>(src_region.height) / dst_region.height;
    const cl_int reg_x = dst_region.x, reg_y = dst_region.y, reg_w = dst_region.width, reg_h = dst_region.height;
    const cl_float max_x = static_cast<float>(src.width) - 0.5f, max_y = static_cast<float>(src.height) - 0.5f;
    const cl_uchar4 fill = {{static_cast<cl_uchar>((background >> 16) & 0xff),
                             static_cast<cl_uchar>((background >> 8) & 0xff), static_cast<cl_uchar>(background & 0xff),
                             0}};
    const cl_int rgb = _rgb ? 1 : 0;

    cl_uint arg = 0;
    cl_int err = CL_SUCCESS;
    err |= clSetKernelArg(_kernel, arg++, sizeof(cl_mem), &planes[0]);
    err |= clSetKernelArg(_kernel, arg++, sizeof(cl_mem), &planes[1]);
    err |= clSetKernelArg(_kernel, arg++, sizeof(cl_mem), &dst);
    err |= clSetKernelArg(_kernel, arg++, sizeof(dst_offset), &dst_offset);
    err |= clSetKernelArg(_kernel, arg++, sizeof(dst_w), &dst_w);
    err |= clSetKernelArg(_kernel, arg++, sizeof(dst_h), &dst_h);
    err |= clSetKernelArg(_kernel, arg++, sizeof(src_x), &src_x);
    err |= clSetKernelArg(_kernel, arg++, sizeof(src_y), &src_y);
    err |= clSetKernelArg(_kernel, arg++, sizeof(scale_x), &scale_x);
    err |= clSetKernelArg(_kernel, arg++, sizeof(scale_y), &scale_y);
    err |= clSetKernelArg(_kernel, arg++, sizeof(reg_x), &reg_x);
    err |= clSetKernelArg(_kernel, arg++, sizeof(reg_y), &reg_y);
    err |= clSetKernelArg(_kernel, arg++, sizeof(reg_w), &reg_w);
    err |= clSetKernelArg(_kernel, arg++, sizeof(reg_h), &reg_h);
    err |= clSetKernelArg(_kernel, arg++, sizeof(max_x), &max_x);
    err |= clSetKernelArg(_kernel, arg++, sizeof(max_y), &max_y);
    err |= clSetKernelArg(_kernel, arg++, sizeof(fill), &fill);
    err |= clSetKernelArg(_kernel, arg++, sizeof(rgb), &rgb);
    ClCheck(err, "clSetKernelArg");

    const size_t global[2] = {_dst_width, _dst_height};
    ClCheck(_acquire(_queue, 2, planes.data(), 0, nullptr, nullptr), "clEnqueueAcquireVA_APIMediaSurfacesINTEL");
    ClCheck(clEnqueueNDRangeKernel(_queue, _kernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr),
            "clEnqueueNDRangeKernel");
    ClCheck(_release(_queue, 2, planes.data(), 0, nullptr, nullptr), "clEnqueueReleaseVA_APIMediaSurfacesINTEL");
}

void VaRoiGpuPreproc::Finish() {
    ClCheck(clFinish(_queue), "clFinish");
}

} // namespace InferenceBackend
