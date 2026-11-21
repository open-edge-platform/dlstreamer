/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "opencl_context.h"
#include "opencl_utils.h"

#include "dlstreamer/base/context.h"
#include "inference_backend/logger.h"

#ifdef _WIN32
#include "dlstreamer/d3d11/device_lock.h"
#include "dlstreamer/d3d11/opencl_sharing.h"
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/d3d11/gstd3d11.h>
#else
#include <CL/cl_va_api_media_sharing_intel.h>
#endif

#include <stdexcept>
#include <vector>

namespace InferenceBackend {

namespace {

constexpr cl_uint kIntelVendorId = 0x8086;

std::vector<cl_platform_id> Platforms() {
    cl_uint count = 0;
    if (clGetPlatformIDs(0, nullptr, &count) != CL_SUCCESS || count == 0)
        throw std::runtime_error("OpenCL pre-processing: no OpenCL platform");
    std::vector<cl_platform_id> platforms(count);
    opencl::Check(clGetPlatformIDs(count, platforms.data(), nullptr), "clGetPlatformIDs");
    return platforms;
}

cl_context CreateContext(const cl_context_properties *props, cl_device_id device) {
    cl_int err = CL_SUCCESS;
    cl_context context = clCreateContext(props, 1, &device, nullptr, nullptr, &err);
    if (err != CL_SUCCESS && context) {
        clReleaseContext(context);
        context = nullptr;
    }
    return context;
}

// The first Intel GPU.
cl_context CreateDefaultGpuContext() {
    for (cl_platform_id platform : Platforms()) {
        cl_uint count = 0;
        if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &count) != CL_SUCCESS || count == 0)
            continue;
        std::vector<cl_device_id> devices(count);
        if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, count, devices.data(), nullptr) != CL_SUCCESS)
            continue;
        for (cl_device_id device : devices) {
            cl_uint vendor = 0;
            if (clGetDeviceInfo(device, CL_DEVICE_VENDOR_ID, sizeof(vendor), &vendor, nullptr) != CL_SUCCESS ||
                vendor != kIntelVendorId)
                continue;
            const cl_context_properties props[] = {CL_CONTEXT_PLATFORM,
                                                   reinterpret_cast<cl_context_properties>(platform), 0};
            if (cl_context context = CreateContext(props, device))
                return context;
        }
    }
    throw std::runtime_error("OpenCL pre-processing: no Intel GPU with OpenCL");
}

#ifndef _WIN32
cl_context CreateVaContext(void *va_display) {
    for (cl_platform_id platform : Platforms()) {
        auto get_device_ids = opencl::GetExtensionFunction<clGetDeviceIDsFromVA_APIMediaAdapterINTEL_fn>(
            platform, "clGetDeviceIDsFromVA_APIMediaAdapterINTEL");
        if (!get_device_ids)
            continue;
        cl_device_id device = nullptr;
        cl_uint count = 0;
        if (get_device_ids(platform, CL_VA_API_DISPLAY_INTEL, va_display, CL_PREFERRED_DEVICES_FOR_VA_API_INTEL, 1,
                           &device, &count) != CL_SUCCESS ||
            count == 0)
            continue;
        const cl_context_properties props[] = {CL_CONTEXT_VA_API_DISPLAY_INTEL,
                                               reinterpret_cast<cl_context_properties>(va_display),
                                               CL_CONTEXT_INTEROP_USER_SYNC,
                                               CL_FALSE,
                                               CL_CONTEXT_PLATFORM,
                                               reinterpret_cast<cl_context_properties>(platform),
                                               0};
        if (cl_context context = CreateContext(props, device))
            return context;
    }
    throw std::runtime_error("OpenCL pre-processing: no OpenCL device shares surfaces with the VA display");
}
#else
cl_context CreateD3D11Context(GstD3D11Device *gst_device) {
    ID3D11Device *device = gst_d3d11_device_get_device_handle(gst_device);
    for (cl_platform_id platform : Platforms()) {
        const dlstreamer::D3D11SharingFunctions sharing(platform);
        if (!sharing.get_device_ids)
            continue;
        cl_device_id cl_device = nullptr;
        cl_uint count = 0;
        if (sharing.get_device_ids(platform, CL_D3D11_DEVICE_KHR, device, CL_PREFERRED_DEVICES_FOR_D3D11_KHR, 1,
                                   &cl_device, &count) != CL_SUCCESS ||
            count == 0)
            continue;
        const cl_context_properties props[] = {CL_CONTEXT_D3D11_DEVICE_KHR,
                                               reinterpret_cast<cl_context_properties>(device),
                                               CL_CONTEXT_INTEROP_USER_SYNC,
                                               CL_TRUE,
                                               CL_CONTEXT_PLATFORM,
                                               reinterpret_cast<cl_context_properties>(platform),
                                               0};
        cl_context context = nullptr;
        {
            // Context creation uses the D3D11 immediate context.
            dlstreamer::D3D11DeviceLock lock(gst_device);
            context = CreateContext(props, cl_device);
        }
        if (context)
            return context;
    }
    throw std::runtime_error("OpenCL pre-processing: no OpenCL device shares textures with the D3D11 device");
}
#endif

} // namespace

cl_context CreateOpenclPreprocContext(const dlstreamer::ContextPtr &app_context) {
#ifndef _WIN32
    if (app_context) {
        if (void *va_display = app_context->handle(dlstreamer::BaseContext::key::va_display))
            return CreateVaContext(va_display);
    }
#else
    if (app_context) {
        if (void *device = app_context->handle(dlstreamer::BaseContext::key::d3d11_device))
            return CreateD3D11Context(static_cast<GstD3D11Device *>(device));
    }
#endif
    return CreateDefaultGpuContext();
}

} // namespace InferenceBackend
