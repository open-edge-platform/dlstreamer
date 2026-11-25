/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 300
#endif
#include <CL/cl.h>

#include <stdexcept>
#include <string>

namespace InferenceBackend {
namespace opencl {

inline void Check(cl_int err, const char *what) {
    if (err != CL_SUCCESS)
        throw std::runtime_error(std::string("OpenCL pre-processing: ") + what + " failed, OpenCL error " +
                                 std::to_string(err));
}

inline cl_device_id ContextDevice(cl_context context) {
    cl_device_id device = nullptr;
    Check(clGetContextInfo(context, CL_CONTEXT_DEVICES, sizeof(device), &device, nullptr), "clGetContextInfo");
    if (!device)
        throw std::runtime_error("OpenCL pre-processing: the OpenCL context has no device");
    return device;
}

inline cl_platform_id DevicePlatform(cl_device_id device) {
    cl_platform_id platform = nullptr;
    Check(clGetDeviceInfo(device, CL_DEVICE_PLATFORM, sizeof(platform), &platform, nullptr), "clGetDeviceInfo");
    return platform;
}

inline std::string DeviceName(cl_device_id device) {
    char name[256] = {};
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name) - 1, name, nullptr);
    return name;
}

inline bool HasExtension(cl_device_id device, const char *extension) {
    size_t size = 0;
    if (clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, 0, nullptr, &size) != CL_SUCCESS || size == 0)
        return false;
    std::string extensions(size, '\0');
    if (clGetDeviceInfo(device, CL_DEVICE_EXTENSIONS, size, &extensions[0], nullptr) != CL_SUCCESS)
        return false;
    return (" " + extensions + " ").find(std::string(" ") + extension + " ") != std::string::npos;
}

template <typename Fn>
Fn GetExtensionFunction(cl_platform_id platform, const char *name) {
    return reinterpret_cast<Fn>(clGetExtensionFunctionAddressForPlatform(platform, name));
}

} // namespace opencl
} // namespace InferenceBackend
