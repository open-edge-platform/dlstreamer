/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// OpenVinoImageInference: the pool of infer requests and what surrounds running them: construction, the completion
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
#include "openvino_model_compilation.h"

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

namespace {

void LogOpenVinoVersion() {
    GVA_DEBUG("OpenVINO version: %s", ov::get_openvino_version().buildNumber);
}

} // namespace

void OpenVinoImageInference::SetCompletionCallback(InferSlot *inference_slot) {
    assert(inference_slot && "Infer slot is null");

    // The callback may run on an OpenVINO thread until Close() has waited for every request, which happens before the
    // slots are destroyed.
    auto cb = [=, this](std::exception_ptr ex) {
        ITT_TASK("completion_callback");

        try {
            if (ex) {
                std::string ex_string = fmt::format("exception occurred during inference: {}", ex);
                GVA_ERROR("%s", ex_string.c_str());
                this->handle_error_(inference_slot->buffers);
            } else {
                this->WorkingFunction(inference_slot);
            }
        } catch (const std::exception &e) {
            GVA_ERROR("An error occurred at inference request completion callback:\n%s",
                      Utils::createNestedErrorMsg(e).c_str());
        }

        FreeSlot(inference_slot);
    };
    inference_slot->infer_request.set_callback(cb);
}

OpenVinoImageInference::OpenVinoImageInference(const InferenceBackend::InferenceConfig &config, Allocator *,
                                               dlstreamer::ContextPtr context, CallbackFunc callback,
                                               ErrorHandlingFunc error_handler, MemoryType memory_type)
    : context_(context), memory_type_(memory_type), callback_(callback), handle_error_(error_handler), batch_size_(0),
      batch_timeout_(-1), frames_in_flight_(0U) {

    try {
        ConfigHelper cfg_helper(config);
        const auto pp_type = cfg_helper.PpType();
        LogOpenVinoVersion();

        const std::string &device = cfg_helper.Device();
        const std::string ov_extension_lib = cfg_helper.OvExtensionLib();
        if (!ov_extension_lib.empty()) {
            OpenVinoCore().add_extension(ov_extension_lib);
        }

        // read the model, prepare it for the pipeline, then compile it on the device
        model_path_ = cfg_helper.ModelPath();
        prepared_model_ = PrepareModel(OpenVinoCore().read_model(model_path_), cfg_helper, memory_type);

        const auto remote_context = CreateRemoteContext(device, memory_type, context);
        img_to_tensor_ = std::make_unique<ImageToTensorConverter>(remote_context);
        compiled_model_ = CompileModelForDevice(prepared_model_, cfg_helper, device, memory_type, remote_context);

        nireq_ = cfg_helper.Nireq();
        if (!nireq_)
            nireq_ = compiled_model_.get_property(ov::optimal_number_of_infer_requests);
        GVA_DEBUG("Num of inference req: %d", nireq_);

        model_name_ = prepared_model_.model->get_friendly_name();
        batch_size_ = prepared_model_.batch_size;
        batch_timeout_ = prepared_model_.batch_timeout;
        image_layer_ = prepared_model_.image_input_name;

        for (int i = 0; i < nireq_; i++) {
            auto &inference_slot = slots_.emplace_back(std::make_unique<InferSlot>());
            inference_slot->infer_request = compiled_model_.create_infer_request();
            inference_slot->in_tensors.resize(prepared_model_.model->inputs().size());
            SetCompletionCallback(inference_slot.get());
            free_slots_.Release(inference_slot.get());
        }

        if (pp_type == InferenceBackend::ImagePreprocessorType::OPENCV ||
            pp_type == InferenceBackend::ImagePreprocessorType::D3D11 ||
            pp_type == InferenceBackend::ImagePreprocessorType::D3D12) {
            std::string pp_type_string = fmt::format("creating pre-processor, type: {}", pp_type);
            GVA_INFO("%s", pp_type_string.c_str());
            const std::string custom_preproc_lib = cfg_helper.CustomPreprocLib();
            pre_processor_.reset(InferenceBackend::ImagePreprocessor::Create(pp_type, custom_preproc_lib));
        }

    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to construct OpenVinoImageInference"));
    }
}

OpenVinoImageInference::~OpenVinoImageInference() {
    GVA_DEBUG("Image Inference destruct");
    Close();
}

void OpenVinoImageInference::FreeSlot(InferSlot *inference_slot) {
    const size_t frame_count = inference_slot->buffers.size();
    inference_slot->buffers.clear();
    for (auto &in_vec : inference_slot->in_tensors) {
        in_vec.clear();
    }
    free_slots_.Release(inference_slot);
    frames_in_flight_ -= frame_count;
    frames_done_.notify_all();
}

bool OpenVinoImageInference::IsQueueFull() {
    return !current_slot_ && !free_slots_.HasFree();
}

const std::string &OpenVinoImageInference::GetModelName() const {
    return model_name_;
}

size_t OpenVinoImageInference::GetBatchSize() const {
    return safe_convert<size_t>(batch_size_);
}

int OpenVinoImageInference::GetBatchTimeout() const {
    return batch_timeout_;
}

size_t OpenVinoImageInference::GetNireq() const {
    return safe_convert<size_t>(nireq_);
}

void OpenVinoImageInference::GetModelImageInputInfo(size_t &width, size_t &height, size_t &batch_size, int &format,
                                                    int &memory_type) const {
    const ImageInputInfo &info = prepared_model_.image_input;
    width = info.width;
    height = info.height;
    batch_size = info.batch_size;
    format = info.format;
    memory_type = info.memory_type;
}

std::map<std::string, std::vector<size_t>> OpenVinoImageInference::GetModelInputsInfo() const {
    std::map<std::string, std::vector<size_t>> res;
    for (auto node : prepared_model_.model->get_parameters()) {
        auto shape = node->is_dynamic() ? node->get_input_partial_shape(0).get_min_shape() : node->get_shape();
        res.emplace(node->get_friendly_name(), std::move(shape));
    }

    return res;
}

std::map<std::string, std::vector<size_t>> OpenVinoImageInference::GetModelOutputsInfo() const {
    std::map<std::string, std::vector<size_t>> res;
    for (auto &node : prepared_model_.model->outputs()) {
        auto shape = node.get_node()->is_dynamic() ? node.get_partial_shape().get_min_shape() : node.get_shape();
        auto name = node.get_names().size() > 0 ? node.get_any_name() : std::string("output");
        res.emplace(name, std::move(shape));
    }
    return res;
}

std::map<std::string, GstStructure *> OpenVinoImageInference::GetModelInfoPostproc() const {
    auto info = ModelApiConverters::get_model_info_postproc(prepared_model_.model, model_path_);
    return info;
}

std::map<std::string, GstStructure *> OpenVinoImageInference::GetModelInfoPreproc(const std::string model_file,
                                                                                  const gchar *pre_proc_config,
                                                                                  const gchar *ov_extension_lib) {
    if (ov_extension_lib && ov_extension_lib[0] != '\0') {
        OpenVinoCore().add_extension(ov_extension_lib);
    }
    std::shared_ptr<ov::Model> model = OpenVinoCore().read_model(model_file);

    auto info = ModelApiConverters::get_model_info_preproc(std::move(model), model_file, pre_proc_config);
    return info;
}

std::map<std::string, std::vector<size_t>> OpenVinoImageInference::GetModelInputShapes(const std::string model_file,
                                                                                       const gchar *ov_extension_lib) {
    if (ov_extension_lib && ov_extension_lib[0] != '\0') {
        OpenVinoCore().add_extension(ov_extension_lib);
    }
    std::shared_ptr<ov::Model> model = OpenVinoCore().read_model(model_file);

    std::map<std::string, std::vector<size_t>> res;
    for (const auto &input : model->inputs()) {
        const auto &partial_shape = input.get_partial_shape();
        const auto &shape = partial_shape.is_dynamic() ? partial_shape.get_min_shape() : partial_shape.get_shape();
        const std::string name = input.get_names().size() > 0 ? input.get_any_name() : std::string("input");
        res.emplace(name, std::vector<size_t>(shape.begin(), shape.end()));
    }
    return res;
}

bool OpenVinoImageInference::IsNpuStridedInputSupported() {
    try {
        const auto props = OpenVinoCore().get_property("NPU", ov::supported_properties);
        return std::find(props.begin(), props.end(), ov::intel_npu::enable_strides_for.name()) != props.end();
    } catch (const std::exception &e) {
        GVA_WARNING("Could not query NPU supported properties: %s", e.what());
        return false;
    }
}

void OpenVinoImageInference::Flush() {
    ITT_TASK(__FUNCTION__);

    // because Flush can execute by several threads for one InferenceImpl instance
    // it must be synchronous.
    std::unique_lock<std::mutex> requests_lk(requests_mutex_);

    std::unique_lock<std::mutex> flush_lk(flush_mutex_);

    if (InferSlot *inference_slot = current_slot_.exchange(nullptr)) {
        if (inference_slot->buffers.empty()) {
            free_slots_.Release(inference_slot);
        } else {
            try {
                // WA: Fill non-complete batch with last element for bypass preprocessing only.
                // When software preprocessing is used, frames are written directly into the infer-request-owned
                // batched tensor, so any unfilled tail slots in the last partial batch already contain data from
                // a previous request reuse.
                if (batch_size_ > 1 && !DoNeedImagePreProcessing(nullptr)) {
                    size_t input_idx = 0;
                    for (auto &input_vec : inference_slot->in_tensors) {
                        for (int i = input_vec.size(); i < batch_size_; i++)
                            input_vec.push_back(input_vec.back());
                        // FIXME: move?
                        inference_slot->infer_request.set_input_tensors(input_idx, input_vec);
                        input_idx++;
                    }
                }

                inference_slot->StartAsync();
            } catch (const std::exception &e) {
                GVA_ERROR("Couldn't start inference on flush: %s", e.what());
                this->handle_error_(inference_slot->buffers);
                FreeSlot(inference_slot);
            }
        }
    }

    while (frames_in_flight_ != 0) {
        // wait_for unlocks flush_mutex_ until we get notify
        // waiting will be continued if frames_in_flight_ != 0
        frames_done_.wait_for(flush_lk, std::chrono::seconds(1), [this] { return frames_in_flight_ == 0; });
    }
}

void OpenVinoImageInference::Close() {
    // Wake a thread that waits in SubmitImage for a free request, so it does not block the Flush below.
    free_slots_.Close();
    Flush();
    for (auto &inference_slot : slots_) {
        inference_slot->infer_request.set_callback([](std::exception_ptr) {});
    }
}

void OpenVinoImageInference::WorkingFunction(InferSlot *inference_slot) {
    assert(inference_slot);

    std::map<std::string, OutputBlob::Ptr> output_blobs;
    const auto &outputs = compiled_model_.outputs();
    for (size_t i = 0; i < outputs.size(); i++) {
        auto name = outputs[i].get_names().size() > 0 ? outputs[i].get_any_name() : std::string("output");
        output_blobs[name] = std::make_shared<OpenvinoOutputTensor>(inference_slot->infer_request.get_output_tensor(i));
    }
    callback_(output_blobs, inference_slot->buffers);
}
