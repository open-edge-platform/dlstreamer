/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/logger.h"

#include "dlstreamer/opencl/command_queue.h"

#include <string>

namespace InferenceBackend {

// Creates the OpenCL queue used for surface-sharing interop copies (D3D11/D3D12 texture ->
// OpenVINO input buffer). The CPU waits for each copy before handing the buffer to OpenVINO,
// and on the engine OpenVINO's inference kernels run on, every small copy would wait behind
// the inference work in flight (~1.2 ms per ROI with two models on the GPU). So the queue goes
// on another engine where the device has one, see dlstreamer::create_queue_on_other_engine.
inline cl_command_queue CreateInteropCommandQueue(cl_context ctx, cl_device_id dev, cl_int *err) {
    std::string engine, default_engine;
    cl_command_queue queue = dlstreamer::create_queue_on_other_engine(ctx, dev, err, &engine, &default_engine);
    if (queue && engine != default_engine)
        GVA_INFO("Surface sharing: OpenCL interop queue on engine '%s' (inference uses '%s')", engine.c_str(),
                 default_engine.c_str());
    return queue;
}

} // namespace InferenceBackend
