/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// ModelLoader: remote context creation differs per platform (VA-API on Linux, D3D11 on Windows), see
// create_remote_context().

#include "openvino_model_loader.h"

#include "openvino_core.h"
#include "openvino_formatters.h"

#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <openvino/runtime/intel_npu/properties.hpp>

#include <spdlog/fmt/bundled/ranges.h>
#include <spdlog/fmt/fmt.h>

#ifdef _WIN32
#include <dlstreamer/d3d11/context.h>
#endif

#ifndef _WIN32
#ifdef ENABLE_VAAPI
#include <dlstreamer/vaapi/context.h>
#endif
#endif

#include <cassert>
#include <stdexcept>

namespace InferenceBackend {
namespace OpenVino {

ModelLoader::ModelLoader(std::string device, MemoryType memory_type, dlstreamer::ContextPtr app_context)
    : _device(std::move(device)), _memory_type(memory_type), _app_context(std::move(app_context)) {
}

// NPU zero-copy DMA-BUFs use a padded row pitch when the model input width is not 64-byte aligned.
// Let the NPU consume such strided input tensors directly instead of copying them on the CPU.
void ModelLoader::enable_npu_dmabuf_strides(ov::AnyMap &ov_params, const ModelConfigurator &configured_model) {
    const std::string &image_input_name = configured_model.image_input_name();
    if (_memory_type != MemoryType::DMA_BUFFER || _device.find("NPU") == std::string::npos ||
        image_input_name.empty() || ov_params.count(ov::intel_npu::enable_strides_for.name()))
        return;

    size_t width = 0, height = 0, batch = 0;
    int format = 0, memory_type = 0;
    configured_model.get_model_image_input_info(width, height, batch, format, memory_type);
    if (width == 0 || alignDmaBufPitch(safe_convert<uint32_t>(width)) == width)
        return;

    GVA_INFO("NPU zero-copy: model input width %zu is not %u-byte aligned, enabling strided input '%s'", width,
             DMA_BUF_PITCH_ALIGNMENT, image_input_name.c_str());
    ov_params[ov::intel_npu::enable_strides_for.name()] = image_input_name;
}

ov::CompiledModel ModelLoader::load_network(const ConfigHelper &config, const ModelConfigurator &configured_model) {
    ov::AnyMap ov_params = config.inference_cfg();

    // Check if enabling strided input for NPU zero-copy is applicable and required.
    enable_npu_dmabuf_strides(ov_params, configured_model);

    std::string params = fmt::format("Params for compile_model:\n  {}", fmt::join(ov_params, "\n  "));
    GVA_INFO("%s", params.c_str());

    GVA_INFO("Loading network to device %s", _device.c_str());
    if (_openvino_context) {
        GVA_INFO("using remote context");
    }

    ov::CompiledModel compiled_model;
    // print_input_and_outputs_info(*_model);
    try {
        if (_openvino_context) {
            compiled_model =
                openvino_core().compile_model(configured_model.model(), _openvino_context->remote_context(), ov_params);
        } else {
            std::string formatted_device = _device;
            if (configured_model.batch_timeout() > -1) {
                formatted_device = fmt::format("BATCH:{}({})", _device, configured_model.auto_batch_num_requests());
            }
            compiled_model = openvino_core().compile_model(configured_model.model(), formatted_device, ov_params);
        }
    } catch (const std::exception &e) {
        GVA_ERROR("Failed to compile model for device %s: %s", _device.c_str(), e.what());
        std::throw_with_nested(std::runtime_error("Failed to compile model for device " + _device));
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

bool ModelLoader::is_device_gpu() const {
    return _device.find("GPU") != std::string::npos;
}

bool ModelLoader::is_device_multi() const {
    return _device.find("MULTI") != std::string::npos;
}

void ModelLoader::create_remote_context() {
    // FIXME: invert to reduce nesting
    if (is_device_gpu() && !is_device_multi()) {
#ifdef _WIN32
        if (_memory_type == MemoryType::D3D11 || _memory_type == MemoryType::SYSTEM) {
            if (_app_context) {
                try {
                    dlstreamer::D3D11ContextPtr d3d11_ctx = dlstreamer::D3D11Context::create(_app_context);
                    _openvino_context =
                        std::make_shared<dlstreamer::OpenVINOContext>(openvino_core(), _device, d3d11_ctx);
                } catch (std::exception &e) {
                    GVA_ERROR("Exception occurred when creating OpenVINO™ toolkit remote context: %s", e.what());
                    std::throw_with_nested(std::runtime_error("couldn't create OV remote context"));
                }

            } else if (_memory_type == MemoryType::D3D11) {
                throw std::runtime_error("Display must be provided for GPU device with d3d11 backend");
            }
        }
#else
        if (_memory_type == MemoryType::VAAPI || _memory_type == MemoryType::SYSTEM) {
            if (_app_context) {
                try {
                    dlstreamer::VAAPIContextPtr vaapi_ctx = dlstreamer::VAAPIContext::create(_app_context);
                    _openvino_context =
                        std::make_shared<dlstreamer::OpenVINOContext>(openvino_core(), _device, vaapi_ctx);
                } catch (std::exception &e) {
                    GVA_ERROR("Exception occurred when creating OpenVINO™ toolkit remote context: %s", e.what());
                    std::throw_with_nested(std::runtime_error("couldn't create OV remote context"));
                }

            } else if (_memory_type == MemoryType::VAAPI) {
                throw std::runtime_error("Display must be provided for GPU device with vaapi-surface-sharing backend");
            }
        }
#endif
    }
}

} // namespace OpenVino
} // namespace InferenceBackend
