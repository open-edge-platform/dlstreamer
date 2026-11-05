/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// OpenVinoModelAdapter: the constructor runs the steps in the order they depend on each other: read the model,
// configure it, create the GPU remote context, then compile.

#include "openvino_model_adapter.h"

#include "openvino_core.h"

#include "inference_backend/logger.h"

#include <spdlog/fmt/fmt.h>

using namespace InferenceBackend;
using namespace InferenceBackend::OpenVino;

void OpenVinoModelAdapter::log_api_message() {
    try {
        const std::string_view ov_build{ov::get_openvino_version().buildNumber};
        std::string box = fmt::format("\n"
                                      "┌{0:─^{3}}┐\n"
                                      "│{1: ^{3}}│\n"
                                      "│{2: ^{3}}│\n"
                                      "└{0:─^{3}}┘\n",
                                      "", ".:: OpenVINO™ via 2.0 API ::.", ov_build, ov_build.size() + 2);
        GVA_DEBUG("%s", box.c_str());
    } catch (...) {
        GVA_ERROR("Unknown exception in log_api_message");
    }
}

OpenVinoModelAdapter::OpenVinoModelAdapter(const ConfigHelper &config, dlstreamer::ContextPtr context,
                                           ImageInference::CallbackFunc callback,
                                           ImageInference::ErrorHandlingFunc error_handler, MemoryType memory_type)
    : _app_context(std::move(context)), _memory_type(memory_type), _callback(callback), _error_handler(error_handler) {
    log_api_message();

    _device = config.device();
    _nireq = config.nireq();

    auto ov_extension_lib = config.ov_extension_lib();
    if (!ov_extension_lib.empty()) {
        openvino_core().add_extension(ov_extension_lib);
    }

    // read model & configure model
    _model_path = config.model_path();
    auto model = openvino_core().read_model(_model_path);
    _configurator = std::make_unique<ModelConfigurator>(std::move(model), _memory_type);
    _configurator->configure(config);

    _loader = std::make_unique<ModelLoader>(_device, _memory_type, _app_context);
    _loader->create_remote_context();
    _img_to_tensor = std::make_unique<ImageToTensorConverter>(_loader->remote_context());

    // Load nn to device
    _compiled_model = _loader->load_network(config, *_configurator);

    if (!_nireq)
        _nireq = _compiled_model.get_property(ov::optimal_number_of_infer_requests);
    GVA_DEBUG("Num of inference req: %d", _nireq);
}

OpenVinoModelAdapter::~OpenVinoModelAdapter() {
    log_api_message();
}

std::map<std::string, std::vector<size_t>> OpenVinoModelAdapter::get_model_inputs_info() const {
    std::map<std::string, std::vector<size_t>> res;
    for (auto node : model()->get_parameters()) {
        auto shape = node->is_dynamic() ? node->get_input_partial_shape(0).get_min_shape() : node->get_shape();
        res.emplace(node->get_friendly_name(), std::move(shape));
    }

    return res;
}

std::map<std::string, std::vector<size_t>> OpenVinoModelAdapter::get_model_outputs_info() const {
    std::map<std::string, std::vector<size_t>> res;
    for (auto &node : model()->outputs()) {
        auto shape = node.get_node()->is_dynamic() ? node.get_partial_shape().get_min_shape() : node.get_shape();
        auto name = node.get_names().size() > 0 ? node.get_any_name() : std::string("output");
        res.emplace(name, std::move(shape));
    }
    return res;
}
