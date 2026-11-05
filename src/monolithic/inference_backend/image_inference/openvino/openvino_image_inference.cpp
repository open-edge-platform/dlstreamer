/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// OpenVINOImageInference: the pool of infer requests and what surrounds running them: construction, the completion
// callback, Flush/Close and the model queries. Submitting frames is in openvino_image_inference_submit.cpp.

#include "gst/gststructure.h"
#include "inference_backend/image_inference.h"
#include <spdlog/fmt/fmt.h>

#include <openvino/runtime/properties.hpp>

#include <openvino/runtime/intel_npu/properties.hpp>

#include "model_api_converters.h"
#include "openvino_config_helper.h"
#include "openvino_core.h"
#include "openvino_formatters.h"
#include "openvino_image_inference.h"
#include "openvino_model_adapter.h"

#include "config.h"
#include "inference_backend/logger.h"
#include "inference_backend/pre_proc.h"

#include "openvino_blob_wrapper.h"
#include "safe_arithmetic.hpp"
#include "utils.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

using namespace InferenceBackend;
using namespace InferenceBackend::OpenVino;

void OpenVINOImageInference::SetCompletionCallback(std::shared_ptr<BatchRequest> &batch_request) {
    assert(batch_request && "Batch request is null");

    auto cb = [=, this](std::exception_ptr ex) {
        ITT_TASK("completion_callback");

        try {
            if (ex) {
                std::string ex_string = fmt::format("exception occured during inference: {}", ex);
                GVA_ERROR("%s", ex_string.c_str());
                this->handleError(batch_request->buffers);
            } else {
                this->WorkingFunction(batch_request);
            }
        } catch (const std::exception &e) {
            GVA_ERROR("An error occurred at inference request completion callback:\n%s",
                      Utils::createNestedErrorMsg(e).c_str());
        }

        FreeRequest(batch_request);
    };
    batch_request->infer_request.set_callback(cb);
}

OpenVINOImageInference::OpenVINOImageInference(const InferenceBackend::InferenceConfig &config, Allocator *,
                                               dlstreamer::ContextPtr context, CallbackFunc callback,
                                               ErrorHandlingFunc error_handler, MemoryType memory_type)
    : context_(context), memory_type(memory_type), callback(callback), handleError(error_handler),
      batch_size(std::stoi(config.at(KEY_BASE).at(KEY_BATCH_SIZE))), batch_timeout(-1), requests_processing_(0U) {

    try {
        if (config.count(KEY_INFERENCE) > 0) {
            const auto &inference_config = config.at(KEY_INFERENCE);
            if (inference_config.count(ov::auto_batch_timeout.name()) > 0) {
                batch_timeout = std::stoi(inference_config.at(ov::auto_batch_timeout.name()));
            }
        }

        ConfigHelper cfg_helper(config);
        const auto pp_type = cfg_helper.pp_type();
        _impl = std::make_unique<OpenVinoModelAdapter>(cfg_helper, context, callback, error_handler, memory_type);

        model_name = _impl->model()->get_friendly_name();
        nireq = _impl->_nireq;
        batch_size = _impl->batch_size();
        batch_timeout = _impl->batch_timeout();
        image_layer = _impl->image_input_name();

        for (int i = 0; i < nireq; i++) {
            std::shared_ptr<BatchRequest> batch_request = std::make_shared<BatchRequest>();
            batch_request->infer_request = _impl->_compiled_model.create_infer_request();
            batch_request->in_tensors.resize(_impl->model()->inputs().size());
            SetCompletionCallback(batch_request);
            freeRequests.push(batch_request);
        }

        if (pp_type == InferenceBackend::ImagePreprocessorType::OPENCV ||
            pp_type == InferenceBackend::ImagePreprocessorType::D3D11) {
            std::string pp_type_string = fmt::format("creating pre-processor, type: {}", pp_type);
            GVA_INFO("%s", pp_type_string.c_str());
            const std::string custom_preproc_lib = cfg_helper.custom_preproc_lib();
            pre_processor.reset(InferenceBackend::ImagePreprocessor::Create(pp_type, custom_preproc_lib));
        }

    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to construct OpenVINOImageInference"));
    }
}

OpenVINOImageInference::~OpenVINOImageInference() {
    GVA_DEBUG("Image Inference destruct");
    Close();
}

void OpenVINOImageInference::FreeRequest(std::shared_ptr<BatchRequest> request) {
    const size_t buffer_size = request->buffers.size();
    request->buffers.clear();
    for (auto &in_vec : request->in_tensors) {
        in_vec.clear();
    }
    freeRequests.push(request);
    requests_processing_ -= buffer_size;
    request_processed_.notify_all();
}

bool OpenVINOImageInference::IsQueueFull() {
    return freeRequests.empty();
}

const std::string &OpenVINOImageInference::GetModelName() const {
    return model_name;
}

size_t OpenVINOImageInference::GetBatchSize() const {
    return safe_convert<size_t>(batch_size);
}

int OpenVINOImageInference::GetBatchTimeout() const {
    return batch_timeout;
}

size_t OpenVINOImageInference::GetNireq() const {
    return safe_convert<size_t>(nireq);
}

void OpenVINOImageInference::GetModelImageInputInfo(size_t &width, size_t &height, size_t &batch_size, int &format,
                                                    int &memory_type_) const {
    _impl->get_model_image_input_info(width, height, batch_size, format, memory_type_);
}

std::map<std::string, std::vector<size_t>> OpenVINOImageInference::GetModelInputsInfo() const {
    auto info = _impl->get_model_inputs_info();
    return info;
}

std::map<std::string, std::vector<size_t>> OpenVINOImageInference::GetModelOutputsInfo() const {
    auto info = _impl->get_model_outputs_info();
    return info;
}

std::map<std::string, GstStructure *> OpenVINOImageInference::GetModelInfoPostproc() const {
    auto info = ModelApiConverters::get_model_info_postproc(_impl->model(), _impl->_model_path);
    return info;
}

std::map<std::string, GstStructure *> OpenVINOImageInference::GetModelInfoPreproc(const std::string model_file,
                                                                                  const gchar *pre_proc_config,
                                                                                  const gchar *ov_extension_lib) {
    if (ov_extension_lib && ov_extension_lib[0] != '\0') {
        openvino_core().add_extension(ov_extension_lib);
    }
    std::shared_ptr<ov::Model> model = openvino_core().read_model(model_file);

    auto info = ModelApiConverters::get_model_info_preproc(std::move(model), model_file, pre_proc_config);
    return info;
}

std::map<std::string, std::vector<size_t>> OpenVINOImageInference::GetModelInputShapes(const std::string model_file,
                                                                                       const gchar *ov_extension_lib) {
    if (ov_extension_lib && ov_extension_lib[0] != '\0') {
        openvino_core().add_extension(ov_extension_lib);
    }
    std::shared_ptr<ov::Model> model = openvino_core().read_model(model_file);

    std::map<std::string, std::vector<size_t>> res;
    for (const auto &input : model->inputs()) {
        const auto &partial_shape = input.get_partial_shape();
        const auto &shape = partial_shape.is_dynamic() ? partial_shape.get_min_shape() : partial_shape.get_shape();
        const std::string name = input.get_names().size() > 0 ? input.get_any_name() : std::string("input");
        res.emplace(name, std::vector<size_t>(shape.begin(), shape.end()));
    }
    return res;
}

bool OpenVINOImageInference::IsNpuStridedInputSupported() {
    try {
        const auto props = openvino_core().get_property("NPU", ov::supported_properties);
        return std::find(props.begin(), props.end(), ov::intel_npu::enable_strides_for.name()) != props.end();
    } catch (const std::exception &e) {
        GVA_WARNING("Could not query NPU supported properties: %s", e.what());
        return false;
    }
}

int OpenVINOImageInference::GetDeviceOptimalBatchSize(const std::string &device) {
    try {
        return static_cast<int>(openvino_core().get_property(device, ov::optimal_batch_size));
    } catch (...) {
        return 1; // Fallback if optimal batch size property is not supported
    }
}

void OpenVINOImageInference::Flush() {
    ITT_TASK(__FUNCTION__);

    // because Flush can execute by several threads for one InferenceImpl instance
    // it must be synchronous.
    std::unique_lock<std::mutex> requests_lk(requests_mutex_);

    std::unique_lock<std::mutex> flush_lk(flush_mutex);

    while (requests_processing_ != 0) {
        auto request = freeRequests.pop();

        if (request->buffers.size() > 0) {
            try {
                // WA: Fill non-complete batch with last element for bypass preprocessing only.
                // When software preprocessing is used, frames are written directly into the infer-request-owned
                // batched tensor, so any unfilled tail slots in the last partial batch already contain data from a
                // previous request reuse.
                if (batch_size > 1 && !DoNeedImagePreProcessing(nullptr)) {
                    size_t input_idx = 0;
                    for (auto &input_vec : request->in_tensors) {
                        for (int i = input_vec.size(); i < batch_size; i++)
                            input_vec.push_back(input_vec.back());
                        // FIXME: move?
                        request->infer_request.set_input_tensors(input_idx, input_vec);
                        input_idx++;
                    }
                }

                request->start_async();
            } catch (const std::exception &e) {
                GVA_ERROR("Couldn't start inferece on flush: %s", e.what());
                this->handleError(request->buffers);
                FreeRequest(request);
            }
        } else {
            freeRequests.push(request);
        }

        // wait_for unlocks flush_mutex until we get notify
        // waiting will be continued if requests_processing_ != 0
        request_processed_.wait_for(flush_lk, std::chrono::seconds(1), [this] { return requests_processing_ == 0; });
    }
}

void OpenVINOImageInference::Close() {
    Flush();
    while (!freeRequests.empty()) {
        auto req = freeRequests.pop();
        req->infer_request.set_callback([](std::exception_ptr) {});
    }
}

void OpenVINOImageInference::WorkingFunction(const std::shared_ptr<BatchRequest> &request) {
    assert(request);

    std::map<std::string, OutputBlob::Ptr> output_blobs;
    const auto &outputs = _impl->_compiled_model.outputs();
    for (size_t i = 0; i < outputs.size(); i++) {
        auto name = outputs[i].get_names().size() > 0 ? outputs[i].get_any_name() : std::string("output");
        output_blobs[name] = std::make_shared<OpenvinoOutputTensor>(request->infer_request.get_output_tensor(i));
    }
    callback(output_blobs, request->buffers);
}
