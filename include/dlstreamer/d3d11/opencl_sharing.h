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
#include <CL/cl_d3d11.h>

namespace dlstreamer {

// The cl_khr_d3d11_sharing entry points of an OpenCL platform, each nullptr if
// the platform does not provide it.
struct D3D11SharingFunctions {
    explicit D3D11SharingFunctions(cl_platform_id platform)
        : get_device_ids(lookup<clGetDeviceIDsFromD3D11KHR_fn>(platform, "clGetDeviceIDsFromD3D11KHR")),
          create_from_texture2d(lookup<clCreateFromD3D11Texture2DKHR_fn>(platform, "clCreateFromD3D11Texture2DKHR")),
          acquire(lookup<clEnqueueAcquireD3D11ObjectsKHR_fn>(platform, "clEnqueueAcquireD3D11ObjectsKHR")),
          release(lookup<clEnqueueReleaseD3D11ObjectsKHR_fn>(platform, "clEnqueueReleaseD3D11ObjectsKHR")) {
    }

    clGetDeviceIDsFromD3D11KHR_fn get_device_ids;
    clCreateFromD3D11Texture2DKHR_fn create_from_texture2d;
    clEnqueueAcquireD3D11ObjectsKHR_fn acquire;
    clEnqueueReleaseD3D11ObjectsKHR_fn release;

  private:
    template <typename Fn>
    static Fn lookup(cl_platform_id platform, const char *name) {
        return reinterpret_cast<Fn>(clGetExtensionFunctionAddressForPlatform(platform, name));
    }
};

} // namespace dlstreamer
