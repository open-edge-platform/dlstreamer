/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Compiling a PreparedModel for a device. GPU surface sharing needs an OpenVINO remote context first, which is created
// from the VA-API (Linux) or D3D11/D3D12 (Windows) context of the pipeline.

#pragma once

#include "openvino_config_helper.h"
#include "openvino_prepared_model.h"

#include "inference_backend/image.h"

#include <dlstreamer/context.h>
#include <dlstreamer/openvino/context.h>

#include <openvino/openvino.hpp>

#include <string>

namespace InferenceBackend {
namespace OpenVino {

// Creates the OpenVINO remote context for GPU surface sharing. Returns null for other devices and memory types.
// app_context is the VA-API or D3D11/D3D12 context of the pipeline; it may be null when no GPU surface sharing is used.
dlstreamer::OpenVINOContextPtr CreateRemoteContext(const std::string &device, MemoryType memory_type,
                                                   const dlstreamer::ContextPtr &app_context);

// Creates the OpenVINO remote context for the OpenCL pre-processing (pre-process-backend=opencl): for a single GPU
// device, the context the pre-processing kernels run in as well, sharing the frames of app_context (a VA display or a
// D3D11 device; may be null for system memory frames). Returns null for other devices, and for a GPU when OpenVINO's
// default context fits (no app_context).
dlstreamer::OpenVINOContextPtr CreateOpenclRemoteContext(const std::string &device,
                                                         const dlstreamer::ContextPtr &app_context);

// Compiles the model on the device, using OpenVINO auto-batching or NPU strided input when the model needs them.
// remote_context is the result of CreateRemoteContext() and may be null.
ov::CompiledModel CompileModelForDevice(const PreparedModel &prepared, const ConfigHelper &config,
                                        const std::string &device, MemoryType memory_type,
                                        const dlstreamer::OpenVINOContextPtr &remote_context);

} // namespace OpenVino
} // namespace InferenceBackend
