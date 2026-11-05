/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// OpenVINO backend of ImageInference. Contains:
// - OpenVinoModelAdapter: the compiled model of one inference instance. It reads the model and delegates to
//   ModelConfigurator (prepare), ModelLoader (compile on the device) and ImageToTensorConverter (frames to tensors).
// - OpenVINOImageInference: pool of infer requests, batching, SubmitImage, completion callback and Flush.

#include "gst/gststructure.h"
#include "inference_backend/image_inference.h"
#include <spdlog/fmt/fmt.h>

#include <cstddef>
#include <openvino/runtime/properties.hpp>

#include <openvino/runtime/intel_npu/level_zero/level_zero.hpp>
#include <openvino/runtime/intel_npu/properties.hpp>

#include <dlstreamer/openvino/context.h>
#ifdef _WIN32
#include <dlstreamer/d3d11/context.h>
#endif
// For logger_name
#include <dlstreamer/element.h>

#include <spdlog/fmt/bundled/ranges.h>

#include "model_api_converters.h"
#include "openvino_config_helper.h"
#include "openvino_core.h"
#include "openvino_formatters.h"
#include "openvino_image_inference.h"
#include "openvino_image_to_tensor_converter.h"
#include "openvino_model_configurator.h"
#include "openvino_model_loader.h"

#include "config.h"
#include "inference_backend/logger.h"
#include "inference_backend/pre_proc.h"

#include "openvino_blob_wrapper.h"
#include "safe_arithmetic.hpp"
#include "utils.h"

#ifndef _WIN32
#ifdef ENABLE_VAAPI
#include <dlstreamer/vaapi/context.h>
#ifdef ENABLE_GPU_TILE_AFFINITY
#include "vaapi_utils.h"
#endif
#endif
#endif

#include <functional>
#include <iterator>
#include <mutex>
#include <regex>
#include <stdio.h>
#include <thread>
#include <unordered_map>

using namespace InferenceBackend;
using namespace InferenceBackend::OpenVino;

namespace {

const InputImageLayerDesc::Ptr
getImagePreProcInfo(const std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> &input_preprocessors) {
    const auto image_it = input_preprocessors.find("image");
    if (image_it != input_preprocessors.cend()) {
        const auto description = image_it->second;
        if (description) {
            return description->input_image_preroc_params;
        }
    }

    return nullptr;
}

} // namespace

class OpenVinoModelAdapter {
    friend class OpenVINOImageInference;

    static void log_api_message() {
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
    };

  public:
#ifndef ENABLE_D3D_NPU_COLOR_CONV
    OpenVinoModelAdapter(const ConfigHelper &config, dlstreamer::ContextPtr context,
                         ImageInference::CallbackFunc callback, ImageInference::ErrorHandlingFunc error_handler,
                         MemoryType memory_type)
        : _app_context(std::move(context)), _memory_type(memory_type), _callback(callback),
          _error_handler(error_handler) {
#else
    OpenVinoModelAdapter(const ConfigHelper &config, dlstreamer::ContextPtr context,
                         ImageInference::CallbackFunc callback, ImageInference::ErrorHandlingFunc error_handler,
                         MemoryType memory_type, InferenceBackend::ImagePreprocessorType pp_type)
        : _app_context(std::move(context)), _memory_type(memory_type), _callback(callback),
          _error_handler(error_handler) {
#endif
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
#ifndef ENABLE_D3D_NPU_COLOR_CONV
        _configurator = std::make_unique<ModelConfigurator>(std::move(model), _memory_type);
#else
        _configurator = std::make_unique<ModelConfigurator>(std::move(model), _memory_type, pp_type);
#endif
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

    ~OpenVinoModelAdapter() {
        log_api_message();
    }

    auto get_model_inputs_info() const {
        std::map<std::string, std::vector<size_t>> res;
        for (auto node : model()->get_parameters()) {
            auto shape = node->is_dynamic() ? node->get_input_partial_shape(0).get_min_shape() : node->get_shape();
            res.emplace(node->get_friendly_name(), std::move(shape));
        }

        return res;
    }

    auto get_model_outputs_info() const {
        std::map<std::string, std::vector<size_t>> res;
        for (auto &node : model()->outputs()) {
            auto shape = node.get_node()->is_dynamic() ? node.get_partial_shape().get_min_shape() : node.get_shape();
            auto name = node.get_names().size() > 0 ? node.get_any_name() : std::string("output");
            res.emplace(name, std::move(shape));
        }
        return res;
    }

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
    std::string _model_path;
    std::string _device;
    dlstreamer::ContextPtr _app_context;
    ov::CompiledModel _compiled_model;
    MemoryType _memory_type;

    std::unique_ptr<ModelConfigurator> _configurator;
    std::unique_ptr<ModelLoader> _loader;
    // Created in the constructor once the (optional) GPU remote context exists.
    std::unique_ptr<ImageToTensorConverter> _img_to_tensor;
    int _nireq = 0;

    ImageInference::CallbackFunc _callback;
    ImageInference::ErrorHandlingFunc _error_handler;
};

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
#ifndef ENABLE_D3D_NPU_COLOR_CONV
        _impl = std::make_unique<OpenVinoModelAdapter>(cfg_helper, context, callback, error_handler, memory_type);
#else
        _impl =
            std::make_unique<OpenVinoModelAdapter>(cfg_helper, context, callback, error_handler, memory_type, pp_type);
#endif

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

#ifndef ENABLE_D3D_NPU_COLOR_CONV
        if (pp_type == InferenceBackend::ImagePreprocessorType::OPENCV ||
            pp_type == InferenceBackend::ImagePreprocessorType::D3D11) {
#else
        if (pp_type == InferenceBackend::ImagePreprocessorType::OPENCV) {
#endif
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

Image fill_image(ov::Tensor &tensor, size_t bindex) {
    Image image = Image();
    const auto &dims = tensor.get_shape();
    if (dims.size() < 4)
        throw std::runtime_error("Unsupported layout: dims size less than 4");
    if (dims.size() < 2 || dims[1] > 4)
        throw std::runtime_error("Unsupported layout: NCHW is expected");
    image.width = safe_convert<uint32_t>(dims[3]);
    image.height = safe_convert<uint32_t>(dims[2]);
    if (bindex >= dims[0]) {
        throw std::out_of_range("Image index is out of range in batch blob");
    }
    auto elem_type = tensor.get_element_type();
    size_t plane_size = safe_mul(size_t(safe_mul(image.width, image.height)), elem_type.size());
    size_t buffer_offset = safe_mul(safe_mul(bindex, plane_size), dims[1]);

    image.planes[0] = static_cast<uint8_t *>(tensor.data()) + buffer_offset;
    image.planes[1] = image.planes[0] + plane_size;
    image.planes[2] = image.planes[1] + plane_size;
    image.planes[3] = nullptr;

    image.stride[0] = image.width;
    image.stride[1] = image.width;
    image.stride[2] = image.width;
    image.stride[3] = 0;
    return image;
}

Image map_ov_tensor_to_img(ov::Tensor &tensor, size_t batch_index) {
    ITT_TASK(__FUNCTION__);
    assert(tensor);

    Image image = fill_image(tensor, batch_index);
    switch (tensor.get_element_type()) {
    case ov::element::f32:
        image.format = FourCC::FOURCC_RGBP_F32;
        break;
    case ov::element::u8:
        image.format = FourCC::FOURCC_RGBP;
        break;
    default:
        throw std::runtime_error("Unsupported precision");
    }
    return image;
}

void OpenVINOImageInference::SubmitImageProcessing(const std::string &input_name, std::shared_ptr<BatchRequest> request,
                                                   const Image &src_img, const InputImageLayerDesc::Ptr &pre_proc_info,
                                                   const ImageTransformationParams::Ptr image_transform_info) {
    ITT_TASK(__FUNCTION__);
    assert(request);

    // FIXME: single input
    if (request->in_tensors.front().empty()) {
        request->in_tensors.front().push_back(request->infer_request.get_tensor(input_name));
    }
    const size_t batch_index = request->buffers.size();
    Image dst_img = map_ov_tensor_to_img(request->in_tensors.front().front(), batch_index);
    if (src_img.planes[0] != dst_img.planes[0]) { // only convert if different buffers
        try {
            pre_processor->Convert(src_img, dst_img, pre_proc_info, image_transform_info);
        } catch (const std::exception &e) {
            std::throw_with_nested(std::runtime_error("Failed while software frame preprocessing"));
        }
    }
}

void OpenVINOImageInference::BypassImageProcessing(const std::string &input_name, std::shared_ptr<BatchRequest> request,
                                                   const Image &src_img, size_t batch_size) {
    ITT_TASK(__FUNCTION__);

    auto ov_tensor = _impl->_img_to_tensor->image_to_tensors(src_img);

    if (batch_size > 1) {
        if (ov_tensor.size() != request->in_tensors.size())
            throw std::runtime_error("BypassImageProcessing - unexpected number of tensors!");
        size_t input_idx = 0;
        for (auto &t : ov_tensor) {
            request->in_tensors[input_idx].emplace_back(std::move(t));
            input_idx++;
        }

        if (request->in_tensors.front().size() == batch_size) {
            // FIXME: input name ?
            for (size_t i = 0; i < request->in_tensors.size(); i++) {
                // FIXME: move?
                request->infer_request.set_input_tensors(i, request->in_tensors[i]);
            }
        }
    } else {
        if (ov_tensor.size() == 1) {
            request->infer_request.set_tensor(input_name, ov_tensor.front());
        } else {
            // FIXME
            for (size_t i = 0; i < ov_tensor.size(); i++) {
                request->infer_request.set_input_tensor(i, ov_tensor[i]);
            }
        }
    }
}

bool OpenVINOImageInference::DoNeedImagePreProcessing(const InferenceBackend::ImagePtr image) {
    if (pre_processor.get() != nullptr)
        return true;

    if (image == nullptr)
        return false;

    // OV plugins exhibit low peformance (serialization) when VA pre-processor generates non-continous tensors
    // Add OpenCV pre-rocessor to compress non-continous tensors on top of VA pre-processor which does resize/crop/etc.
    if ((_impl->_memory_type == MemoryType::SYSTEM) &&
        ((image->format == FourCC::FOURCC_RGBP) || (image->format == FourCC::FOURCC_BGRP))) {

        if (!isImageContiguous(*image)) {
            GVA_WARNING("Force OPENCV preprocessor to convert non-contiguous tensors into contigous memory location");
            pre_processor.reset(
                InferenceBackend::ImagePreprocessor::Create(InferenceBackend::ImagePreprocessorType::OPENCV, ""));
            return true;
        }
    }

    return false;
}

void OpenVINOImageInference::ApplyInputPreprocessors(
    std::shared_ptr<BatchRequest> &request, const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors) {
    ITT_TASK(__FUNCTION__);
    assert(request && "Batch request is null");

    const size_t batch_index = request->buffers.size();

    for (const auto &preprocessor : input_preprocessors) {
        if (preprocessor.second == nullptr)
            continue;

        if (preprocessor.first == KEY_image) {
            if (!DoNeedImagePreProcessing(nullptr))
                continue;
        }

        const auto &model_inputs = _impl->model()->inputs();
        std::shared_ptr<OpenvinoInputTensor> intput_tensor = nullptr;
        if ((preprocessor.first == KEY_image) && (model_inputs.size() == 1) && !request->in_tensors.empty() &&
            !request->in_tensors.front().empty()) {
            intput_tensor = std::make_shared<OpenvinoInputTensor>(request->in_tensors.front().front(), batch_index);
        } else if (model_inputs.size() == 1) {
            intput_tensor = std::make_shared<OpenvinoInputTensor>(request->infer_request.get_input_tensor());
        } else {
            intput_tensor =
                std::make_shared<OpenvinoInputTensor>(request->infer_request.get_tensor(preprocessor.second->name));
        }

        preprocessor.second->preprocessor(intput_tensor);
    }
}

void OpenVINOImageInference::SubmitImage(
    IFrameBase::Ptr frame, const std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> &input_preprocessors) {
    ITT_TASK(__FUNCTION__);

    if (!frame)
        throw std::invalid_argument("Invalid frame provided");

    std::unique_lock<std::mutex> lk(requests_mutex_);
    ++requests_processing_;
    std::shared_ptr<BatchRequest> request = freeRequests.pop();

    try {
        if (DoNeedImagePreProcessing(frame->GetImage())) {
            SubmitImageProcessing(
                image_layer, request, *frame->GetImage(),
                getImagePreProcInfo(input_preprocessors), // contain operations order for Custom Image PreProcessing
                frame->GetImageTransformationParams()     // during CIPP will be filling of crop and aspect-ratio
                                                          // parameters
            );
        } else {
            BypassImageProcessing(image_layer, request, *frame->GetImage(), safe_convert<size_t>(batch_size));
        }

        ApplyInputPreprocessors(request, input_preprocessors);

        request->buffers.push_back(frame);
    } catch (const std::exception &e) {
        GVA_ERROR("Pre-processing has failed: %s", e.what());
        std::throw_with_nested(std::runtime_error("Pre-processing was failed."));
    }

    try {
        // start inference asynchronously if enough buffers for batching
        if (request->buffers.size() >= safe_convert<size_t>(batch_size)) {
            request->start_async();
        } else {
            freeRequests.push_front(request);
        }
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Inference async start was failed."));
    }
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
