/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Remote context creation differs per platform (VA-API on Linux, D3D11/D3D12 on Windows), see CreateRemoteContext().

#include "openvino_model_compilation.h"

#include "openvino_core.h"
#include "openvino_formatters.h"

#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <openvino/runtime/intel_npu/properties.hpp>

#include <spdlog/fmt/bundled/ranges.h>
#include <spdlog/fmt/fmt.h>

#ifdef _WIN32
#include "opencl_context.h"
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

bool IsDeviceGpu(const std::string &device) {
    return device.find("GPU") != std::string::npos;
}

bool IsDeviceMulti(const std::string &device) {
    return device.find("MULTI") != std::string::npos;
}

// NPU zero-copy DMA-BUFs use a padded row pitch when the model input width is not 64-byte aligned.
// Let the NPU consume such strided input tensors directly instead of copying them on the CPU.
void EnableNpuDmabufStrides(ov::AnyMap &ov_params, const PreparedModel &prepared, const std::string &device,
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

dlstreamer::OpenVINOContextPtr CreateRemoteContext(const std::string &device, MemoryType memory_type,
                                                   const dlstreamer::ContextPtr &app_context) {
    dlstreamer::OpenVINOContextPtr openvino_context;
    // FIXME: invert to reduce nesting
    if (IsDeviceGpu(device) && !IsDeviceMulti(device)) {
#ifdef _WIN32
        if (memory_type == MemoryType::D3D11 || memory_type == MemoryType::D3D12 || memory_type == MemoryType::SYSTEM) {
            if (app_context) {
                try {
                    if (app_context->memory_type() == dlstreamer::MemoryType::D3D12) {
                        // No D3D12 interop in the OV GPU plugin: use OV's own OpenCL context on the
                        // matching GPU and bridge D3D12 resources through cl_khr_external_memory.
                        dlstreamer::D3D12ContextPtr d3d12_ctx = dlstreamer::D3D12Context::create(app_context);
                        openvino_context =
                            std::make_shared<dlstreamer::OpenVINOContext>(OpenVinoCore(), device, d3d12_ctx);
                    } else {
                        dlstreamer::D3D11ContextPtr d3d11_ctx = dlstreamer::D3D11Context::create(app_context);
                        openvino_context =
                            std::make_shared<dlstreamer::OpenVINOContext>(OpenVinoCore(), device, d3d11_ctx);
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
                    openvino_context = std::make_shared<dlstreamer::OpenVINOContext>(OpenVinoCore(), device, vaapi_ctx);
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

dlstreamer::OpenVINOContextPtr CreateOpenclRemoteContext(const std::string &device,
                                                         const dlstreamer::ContextPtr &app_context) {
    // Only a single GPU device ("GPU", "GPU.<N>") has an OpenCL context the kernels can write the model input in.
    if (device.rfind("GPU", 0) != 0)
        return nullptr;
#ifdef _WIN32
    // OpenVINO's D3D11 context leaves CL_CONTEXT_INTEROP_USER_SYNC off, which makes every texture acquire wait for
    // the GPU on the CPU. The pre-processing orders the D3D11 work itself, so OpenVINO gets a context with user sync.
    if (app_context && app_context->handle(dlstreamer::BaseContext::key::d3d11_device)) {
        cl_context cl_ctx = CreateOpenclPreprocContext(app_context);
        ov::RemoteContext remote_context;
        try {
            // OpenVINO retains the context.
            remote_context = ov::intel_gpu::ocl::ClContext(OpenVinoCore(), cl_ctx);
        } catch (...) {
            clReleaseContext(cl_ctx);
            throw;
        }
        clReleaseContext(cl_ctx);
        GVA_INFO("OpenCL pre-processing: OpenVINO uses an OpenCL context with D3D11 interop user sync");
        return std::make_shared<dlstreamer::OpenVINOContext>(remote_context);
    }
    return nullptr;
#else
    // A VA display gets OpenVINO's VA context; system memory frames OpenVINO's default context.
    return CreateRemoteContext(device, MemoryType::SYSTEM, app_context);
#endif
}

ov::CompiledModel CompileModelForDevice(const PreparedModel &prepared, const ConfigHelper &config,
                                        const std::string &device, MemoryType memory_type,
                                        const dlstreamer::OpenVINOContextPtr &remote_context) {
    ov::AnyMap ov_params = config.InferenceCfg();

    // Check if enabling strided input for NPU zero-copy is applicable and required.
    EnableNpuDmabufStrides(ov_params, prepared, device, memory_type);

    std::string params = fmt::format("Params for compile_model:\n  {}", fmt::join(ov_params, "\n  "));
    GVA_INFO("%s", params.c_str());

    GVA_INFO("Loading network to device %s", device.c_str());
    if (remote_context) {
        GVA_INFO("using remote context");
    }

    ov::CompiledModel compiled_model;
    try {
        if (remote_context) {
            compiled_model = OpenVinoCore().compile_model(prepared.model, remote_context->remote_context(), ov_params);
        } else {
            std::string formatted_device = device;
            if (prepared.batch_timeout > -1) {
                formatted_device = fmt::format("BATCH:{}({})", device, prepared.auto_batch_num_requests);
            }
            compiled_model = OpenVinoCore().compile_model(prepared.model, formatted_device, ov_params);
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
