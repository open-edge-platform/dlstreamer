/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Remote context creation differs per platform (VA-API on Linux, D3D11/D3D12 on Windows), see create_remote_context().

#include "openvino_model_compilation.h"

#include "openvino_core.h"
#include "openvino_formatters.h"

#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <openvino/runtime/intel_npu/properties.hpp>

#include <spdlog/fmt/bundled/ranges.h>
#include <spdlog/fmt/fmt.h>

#ifdef _WIN32
#include <dlstreamer/d3d11/context.h>
#include <dlstreamer/d3d12/context.h>
#endif

#ifndef _WIN32
#ifdef ENABLE_VAAPI
#include <dlstreamer/vaapi/context.h>
#endif
#endif

#include <stdexcept>

namespace InferenceBackend {
namespace OpenVino {

namespace {

bool is_device_gpu(const std::string &device) {
    return device.find("GPU") != std::string::npos;
}

bool is_device_multi(const std::string &device) {
    return device.find("MULTI") != std::string::npos;
}

// NPU zero-copy DMA-BUFs use a padded row pitch when the model input width is not 64-byte aligned.
// Let the NPU consume such strided input tensors directly instead of copying them on the CPU.
void enable_npu_dmabuf_strides(ov::AnyMap &ov_params, const PreparedModel &prepared, const std::string &device,
                               MemoryType memory_type) {
    if (memory_type != MemoryType::DMA_BUFFER || device.find("NPU") == std::string::npos ||
        prepared.image_input_name.empty() || ov_params.count(ov::intel_npu::enable_strides_for.name()))
        return;

    const size_t width = prepared.image_input.width;
    if (width == 0 || alignDmaBufPitch(safe_convert<uint32_t>(width)) == width)
        return;

    GVA_INFO("NPU zero-copy: model input width %zu is not %u-byte aligned, enabling strided input '%s'", width,
             DMA_BUF_PITCH_ALIGNMENT, prepared.image_input_name.c_str());
    ov_params[ov::intel_npu::enable_strides_for.name()] = prepared.image_input_name;
}

} // namespace

dlstreamer::OpenVINOContextPtr create_remote_context(const std::string &device, MemoryType memory_type,
                                                     const dlstreamer::ContextPtr &app_context) {
    dlstreamer::OpenVINOContextPtr openvino_context;
    // FIXME: invert to reduce nesting
    if (is_device_gpu(device) && !is_device_multi(device)) {
#ifdef _WIN32
        if (memory_type == MemoryType::D3D11 || memory_type == MemoryType::D3D12 || memory_type == MemoryType::SYSTEM) {
            if (app_context) {
                try {
                    if (app_context->memory_type() == dlstreamer::MemoryType::D3D12) {
                        // No D3D12 interop in the OV GPU plugin: use OV's own OpenCL context on the
                        // matching GPU and bridge D3D12 resources through cl_khr_external_memory.
                        dlstreamer::D3D12ContextPtr d3d12_ctx = dlstreamer::D3D12Context::create(app_context);
                        openvino_context =
                            std::make_shared<dlstreamer::OpenVINOContext>(openvino_core(), device, d3d12_ctx);
                    } else {
                        dlstreamer::D3D11ContextPtr d3d11_ctx = dlstreamer::D3D11Context::create(app_context);
                        openvino_context =
                            std::make_shared<dlstreamer::OpenVINOContext>(openvino_core(), device, d3d11_ctx);
                    }
                } catch (std::exception &e) {
                    GVA_ERROR("Exception occurred when creating OpenVINO™ toolkit remote context: %s", e.what());
                    std::throw_with_nested(std::runtime_error("couldn't create OV remote context"));
                }

            } else if (memory_type == MemoryType::D3D11 || memory_type == MemoryType::D3D12) {
                throw std::runtime_error("Display must be provided for GPU device with d3d11/d3d12 backend");
            }
        }
#else
        if (memory_type == MemoryType::VAAPI || memory_type == MemoryType::SYSTEM) {
            if (app_context) {
                try {
                    dlstreamer::VAAPIContextPtr vaapi_ctx = dlstreamer::VAAPIContext::create(app_context);
                    openvino_context =
                        std::make_shared<dlstreamer::OpenVINOContext>(openvino_core(), device, vaapi_ctx);
                } catch (std::exception &e) {
                    GVA_ERROR("Exception occurred when creating OpenVINO™ toolkit remote context: %s", e.what());
                    std::throw_with_nested(std::runtime_error("couldn't create OV remote context"));
                }

            } else if (memory_type == MemoryType::VAAPI) {
                throw std::runtime_error("Display must be provided for GPU device with vaapi-surface-sharing backend");
            }
        }
#endif
    }
    return openvino_context;
}

ov::CompiledModel compile_model_for_device(const PreparedModel &prepared, const ConfigHelper &config,
                                           const std::string &device, MemoryType memory_type,
                                           const dlstreamer::OpenVINOContextPtr &remote_context) {
    ov::AnyMap ov_params = config.inference_cfg();

    // Check if enabling strided input for NPU zero-copy is applicable and required.
    enable_npu_dmabuf_strides(ov_params, prepared, device, memory_type);

    std::string params = fmt::format("Params for compile_model:\n  {}", fmt::join(ov_params, "\n  "));
    GVA_INFO("%s", params.c_str());

    GVA_INFO("Loading network to device %s", device.c_str());
    if (remote_context) {
        GVA_INFO("using remote context");
    }

    ov::CompiledModel compiled_model;
    try {
        if (remote_context) {
            compiled_model = openvino_core().compile_model(prepared.model, remote_context->remote_context(), ov_params);
        } else {
            std::string formatted_device = device;
            if (prepared.batch_timeout > -1) {
                formatted_device = fmt::format("BATCH:{}({})", device, prepared.auto_batch_num_requests);
            }
            compiled_model = openvino_core().compile_model(prepared.model, formatted_device, ov_params);
        }
    } catch (const std::exception &e) {
        GVA_ERROR("Failed to compile model for device %s: %s", device.c_str(), e.what());
        std::throw_with_nested(std::runtime_error("Failed to compile model for device " + device));
    }
    GVA_INFO("Network loaded to device");

    auto supported_properties = compiled_model.get_property(ov::supported_properties);
    for (const auto &cfg : supported_properties) {
        if (cfg == ov::supported_properties)
            continue;
        auto prop = compiled_model.get_property(cfg);
        GVA_DEBUG(" %s: %s", cfg.c_str(), prop.as<std::string>().c_str());
    }
    return compiled_model;
}

} // namespace OpenVino
} // namespace InferenceBackend
