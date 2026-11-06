/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Compiling a PreparedModel for a device. GPU surface sharing needs an OpenVINO remote context first, which is created
// from the VA-API (Linux) or D3D11 (Windows) context of the pipeline.

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
// app_context is the VA-API or D3D11 context of the pipeline; it may be null when no GPU surface sharing is used.
dlstreamer::OpenVINOContextPtr create_remote_context(const std::string &device, MemoryType memory_type,
                                                     const dlstreamer::ContextPtr &app_context);

// Compiles the model on the device, using OpenVINO auto-batching or NPU strided input when the model needs them.
// remote_context is the result of create_remote_context() and may be null.
ov::CompiledModel compile_model_for_device(const PreparedModel &prepared, const ConfigHelper &config,
                                           const std::string &device, MemoryType memory_type,
                                           const dlstreamer::OpenVINOContextPtr &remote_context);

} // namespace OpenVino
} // namespace InferenceBackend
