/*******************************************************************************
 * Copyright (C) 2018-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "ov_inference.h"
#include "audio_defs.h"
#include "core_singleton.h"
#include "dlstreamer/openvino/utils.h"
#include "dlstreamer/tensor_info.h"
#include "dlstreamer/utils.h"
#include "image_inference.h"
#include "inference_backend/logger.h"
#include "utils.h"

#include <algorithm>
#include <fstream>
#include <limits.h>
#include <string>

using namespace std;
using namespace InferenceBackend;
using namespace dlstreamer;

namespace {
class OpenvinoOutputTensor : public OutputBlob {
    ov::Tensor _tensor;
    mutable ov::Shape _shape;

  public:
    OpenvinoOutputTensor(ov::Tensor tensor) : _tensor(std::move(tensor)) {
    }

    const std::vector<size_t> &GetDims() const override {
        if (_shape.empty())
            _shape = _tensor.get_shape();
        return _shape;
    }

    // FIXME: remove hardcode
    InferenceBackend::Blob::Layout GetLayout() const override {
        return InferenceBackend::Blob::Layout::NC;
        // return static_cast<InferenceBackend::Blob::Layout>((int)blob->getTensorDesc().getLayout());
        // throw std::runtime_error("unsupported!");
    }

    InferenceBackend::Blob::Precision GetPrecision() const override {
        switch (_tensor.get_element_type()) {
        case ov::element::u8:
            return InferenceBackend::Blob::Precision::U8;
        case ov::element::f32:
            return InferenceBackend::Blob::Precision::FP32;
        case ov::element::f16:
            return InferenceBackend::Blob::Precision::FP16;
        case ov::element::bf16:
            return InferenceBackend::Blob::Precision::BF16;
        case ov::element::f64:
            return InferenceBackend::Blob::Precision::FP64;
        case ov::element::i16:
            return InferenceBackend::Blob::Precision::I16;
        case ov::element::i32:
            return InferenceBackend::Blob::Precision::I32;
        case ov::element::i64:
            return InferenceBackend::Blob::Precision::I64;
        case ov::element::u4:
            return InferenceBackend::Blob::Precision::U4;
        case ov::element::u16:
            return InferenceBackend::Blob::Precision::U16;
        case ov::element::u32:
            return InferenceBackend::Blob::Precision::U32;
        case ov::element::u64:
            return InferenceBackend::Blob::Precision::U64;

        default:
            throw std::runtime_error(std::string("unsupported element type: ") +
                                     _tensor.get_element_type().get_type_name().c_str());
        }
    }

    const void *GetData() const override {
        return _tensor.data();
    }
};
} // namespace

OpenVINOAudioInference::OpenVINOAudioInference(const std::string &model_path, const std::string &device,
                                               AudioInferenceOutput &infOutput) {

    // std::map<std::string, std::string> base;
    // std::map<std::string, std::string> inference_config;
    // base[KEY_DEVICE] = device;

    // if (!InferenceBackend::ModelLoader::is_valid_model_path(model_path))
    //     throw std::runtime_error("Invalid model path.");

    _model = _core.read_model(model_path);
    infOutput.model_name = _model->get_friendly_name();

    // std::cout << "Params for compile_model:\n";
    // print_ov_map(ov_params);
    // auto ov_params = string_to_openvino_map(config);
    // adjust_ie_config(ov_params); // TODO Do we need it?
    // _compiled_model = _core.compile_model(_model, device, ov_params);
    _compiled_model = _core.compile_model(_model, device);
    _infer_request = _compiled_model.create_infer_request();

    _model_input_info = FrameInfo(MediaType::Tensors);
    for (auto node : _model->get_parameters()) {
        auto dtype = data_type_from_openvino(node->get_element_type());
        auto shape = node->is_dynamic() ? node->get_input_partial_shape(0).get_min_shape() : node->get_shape();
        _model_input_info.tensors.push_back(TensorInfo(shape, dtype));
    }

    const auto &outputs = _compiled_model.outputs();
    std::map<std::string, OutputBlob::Ptr> output_tensors;
    for (size_t i = 0; i < outputs.size(); ++i) {
        output_tensors[outputs[i].get_any_name()] =
            std::make_shared<OpenvinoOutputTensor>(_infer_request.get_output_tensor(i));
    }

    infOutput.output_tensors = output_tensors;
    infOut = infOutput;
}

std::vector<uint8_t> OpenVINOAudioInference::convertFloatToU8(std::vector<float> &normalized_samples) {
    if (normalized_samples.empty())
        throw std::invalid_argument("Invalid Input buffer");

    const auto &tensor_info = _model_input_info.tensors.at(0);

    switch (tensor_info.dtype) {
    case DataType::UInt8: {
        std::vector<uint8_t> data_after_fq(normalized_samples.size());
        transform(normalized_samples.begin(), normalized_samples.end(), data_after_fq.begin(), [](float v) {
            float fq = ((v - FQ_PARAMS_MIN) / FQ_PARAMS_SCALE) * 255;
            fq = std::max(0.f, std::min(255.f, fq));
            return fq;
        });
        return data_after_fq;
    }
    case DataType::Float32:
        return {};
    default:
        throw std::invalid_argument(datatype_to_string(tensor_info.dtype) + " is not supported");
    }
}

// TODO: VPU enabling?
void OpenVINOAudioInference::setInputBlob(void *buffer_ptr, int dma_fd) {
    if (!buffer_ptr)
        throw std::invalid_argument("Invalid input buffer");

    const auto &tensor_info = _model_input_info.tensors.at(0);

    ov::Tensor input_tensor = ov::Tensor(data_type_to_openvino(tensor_info.dtype), tensor_info.shape, buffer_ptr);

    UNUSED(dma_fd);

    _infer_request.set_input_tensor(input_tensor);
}

AudioInferenceOutput *OpenVINOAudioInference::getInferenceOutput() {
    return &infOut;
}

void OpenVINOAudioInference::infer() {
    _infer_request.infer();
}

void OpenVINOAudioInference::CreateRemoteContext(const std::string & /* device */) {
}
