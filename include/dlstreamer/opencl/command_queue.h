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
#include <CL/cl_ext.h>

#include <string>
#include <vector>

namespace dlstreamer {

// Creates an in-order command queue on a hardware engine other than the one a
// default queue lands on, if the device has another engine that runs every
// command (cl_intel_command_queue_families), e.g. 'rcs' next to 'ccs'.
// Otherwise returns a default queue.
//
// A default queue shares its engine with everything else the process runs on
// the device, OpenVINO inference in particular, so work the CPU waits for
// right after submitting it also waits for whatever is in flight there.
//
// `engine` and `default_engine`, if given, receive the names of the engine the
// queue is on and of the one a default queue uses: equal if the queue stayed on
// the default engine, empty if the device does not report engines. Returns
// nullptr, with the error in `*err`, if not even a default queue can be created.
inline cl_command_queue create_queue_on_other_engine(cl_context context, cl_device_id device, cl_int *err,
                                                     std::string *engine = nullptr,
                                                     std::string *default_engine = nullptr) {
    cl_command_queue queue = clCreateCommandQueueWithProperties(context, device, nullptr, err);
    if (!queue || *err != CL_SUCCESS)
        return nullptr;

#ifdef CL_DEVICE_QUEUE_FAMILY_PROPERTIES_INTEL
    size_t size = 0;
    if (clGetDeviceInfo(device, CL_DEVICE_QUEUE_FAMILY_PROPERTIES_INTEL, 0, nullptr, &size) != CL_SUCCESS || size == 0)
        return queue;
    std::vector<cl_queue_family_properties_intel> families(size / sizeof(cl_queue_family_properties_intel));
    cl_uint default_family = 0;
    if (clGetDeviceInfo(device, CL_DEVICE_QUEUE_FAMILY_PROPERTIES_INTEL, size, families.data(), nullptr) !=
            CL_SUCCESS ||
        clGetCommandQueueInfo(queue, CL_QUEUE_FAMILY_INTEL, sizeof(default_family), &default_family, nullptr) !=
            CL_SUCCESS ||
        default_family >= families.size())
        return queue;
    if (engine)
        *engine = families[default_family].name;
    if (default_engine)
        *default_engine = families[default_family].name;

    for (cl_uint i = 0; i < families.size(); ++i) {
        // Default capabilities mean the engine runs every command, kernels and
        // image copies included, which copy engines do not.
        if (i == default_family || families[i].capabilities != CL_QUEUE_DEFAULT_CAPABILITIES_INTEL ||
            families[i].count == 0)
            continue;
        const cl_queue_properties props[] = {CL_QUEUE_FAMILY_INTEL, i, CL_QUEUE_INDEX_INTEL, 0, 0};
        cl_int other_err = CL_SUCCESS;
        cl_command_queue other = clCreateCommandQueueWithProperties(context, device, props, &other_err);
        if (other && other_err == CL_SUCCESS) {
            clReleaseCommandQueue(queue);
            if (engine)
                *engine = families[i].name;
            return other;
        }
    }
#endif
    return queue;
}

} // namespace dlstreamer
