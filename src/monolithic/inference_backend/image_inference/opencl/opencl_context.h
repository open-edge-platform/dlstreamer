/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/context.h"

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 300
#endif
#include <CL/cl.h>

namespace InferenceBackend {

// Creates an OpenCL context for pre-processing the frames of `app_context`, on the GPU the frames are on:
//   - a VA display (Linux): the context shares the VA surfaces of the display;
//   - a D3D11 device (Windows): the context shares textures with the device, with CL_CONTEXT_INTEROP_USER_SYNC, so
//     that acquiring a texture does not make the CPU wait for the GPU (the caller orders the D3D11 work itself);
//   - none: a context on the first Intel GPU.
// The caller owns the returned context. Throws if there is no suitable GPU.
cl_context CreateOpenclPreprocContext(const dlstreamer::ContextPtr &app_context);

} // namespace InferenceBackend
