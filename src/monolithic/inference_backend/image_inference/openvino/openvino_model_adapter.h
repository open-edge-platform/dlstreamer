/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// OpenVinoModelAdapter is the compiled model of one OpenVINO inference instance, with the DL Streamer side of it:
// it reads the model, lets ModelConfigurator prepare it, ModelLoader compile it on the device, and keeps the
// ImageToTensorConverter that turns frames into input tensors. OpenVINOImageInference owns one and runs requests on it.

#pragma once

#include "openvino_config_helper.h"
#include "openvino_image_to_tensor_converter.h"
#include "openvino_model_configurator.h"
#include "openvino_model_loader.h"

#include "inference_backend/image_inference.h"

#include "config.h"

#include <dlstreamer/context.h>

#include <openvino/openvino.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

class OpenVinoModelAdapter {
    friend class OpenVINOImageInference;

  public:
    OpenVinoModelAdapter(const InferenceBackend::OpenVino::ConfigHelper &config, dlstreamer::ContextPtr context,
                         InferenceBackend::ImageInference::CallbackFunc callback,
                         InferenceBackend::ImageInference::ErrorHandlingFunc error_handler,
                         InferenceBackend::MemoryType memory_type);

    ~OpenVinoModelAdapter();

    std::map<std::string, std::vector<size_t>> get_model_inputs_info() const;
    std::map<std::string, std::vector<size_t>> get_model_outputs_info() const;

    void get_model_image_input_info(size_t &width, size_t &height, size_t &batch_size, int &format,
                                    int &memory_type) const {
        _configurator->get_model_image_input_info(width, height, batch_size, format, memory_type);
    }

    const std::shared_ptr<ov::Model> &model() const {
        return _configurator->model();
    }

    int batch_size() const {
        return _configurator->batch_size();
    }

    int batch_timeout() const {
        return _configurator->batch_timeout();
    }

    const std::string &image_input_name() const {
        return _configurator->image_input_name();
    }

  protected:
    static void log_api_message();

    std::string _model_path;
    std::string _device;
    dlstreamer::ContextPtr _app_context;
    ov::CompiledModel _compiled_model;
    InferenceBackend::MemoryType _memory_type;

    std::unique_ptr<InferenceBackend::OpenVino::ModelConfigurator> _configurator;
    std::unique_ptr<InferenceBackend::OpenVino::ModelLoader> _loader;
    // Created in the constructor once the (optional) GPU remote context exists.
    std::unique_ptr<InferenceBackend::OpenVino::ImageToTensorConverter> _img_to_tensor;
    int _nireq = 0;

    InferenceBackend::ImageInference::CallbackFunc _callback;
    InferenceBackend::ImageInference::ErrorHandlingFunc _error_handler;
};
