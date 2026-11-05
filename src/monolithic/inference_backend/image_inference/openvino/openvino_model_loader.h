/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// ModelLoader puts a configured model on the device: it creates the GPU remote context when surface sharing needs
// one, and compiles the model (with OpenVINO auto-batching or NPU strided input when required).

#pragma once

#include "openvino_config_helper.h"
#include "openvino_model_configurator.h"

#include "inference_backend/image.h"

#include <dlstreamer/context.h>
#include <dlstreamer/openvino/context.h>

#include <openvino/openvino.hpp>

#include <string>

namespace InferenceBackend {
namespace OpenVino {

class ModelLoader {
  public:
    // app_context is the VA-API or D3D11 context of the pipeline; it may be null when no GPU surface sharing is used.
    ModelLoader(std::string device, MemoryType memory_type, dlstreamer::ContextPtr app_context);

    // Creates the OpenVINO remote context for GPU surface sharing. Does nothing (the context stays null) for other
    // devices and memory types. Call it before load_network().
    void create_remote_context();

    // Null unless create_remote_context() created a context.
    const dlstreamer::OpenVINOContextPtr &remote_context() const {
        return _openvino_context;
    }

    // Compiles the configured model for the device.
    ov::CompiledModel load_network(const ConfigHelper &config, const ModelConfigurator &configured_model);

  private:
    void enable_npu_dmabuf_strides(ov::AnyMap &ov_params, const ModelConfigurator &configured_model);
    bool is_device_gpu() const;
    bool is_device_multi() const;

    std::string _device;
    MemoryType _memory_type;
    dlstreamer::ContextPtr _app_context;
    dlstreamer::OpenVINOContextPtr _openvino_context;
};

} // namespace OpenVino
} // namespace InferenceBackend
