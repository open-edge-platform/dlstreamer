/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "label.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"
#include "tensor.h"

#include <gst/gst.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace post_processing;
using namespace InferenceBackend;

namespace {

// Function to find the maximum element in the data and set the corresponding label, label_id, and confidence.
template <typename DataType>
void max_method(const DataType *data, size_t size, const std::vector<std::string> &labels, GVA::Tensor &result) {
    auto max_elem = std::max_element(data, data + size);
    auto index = std::distance(data, max_elem);
    result.set_string("label", labels.at(index));
    result.set_int("label_id", index);
    result.set_double("confidence", *max_elem);
}

// Function to apply softmax to the data and set the label with the highest probability.
template <typename DataType>
void soft_max_method(const DataType *data, size_t size, const std::vector<std::string> &labels, GVA::Tensor &result) {
    auto max_confidence = std::max_element(data, data + size);
    std::vector<float> sftm_arr(size);
    float sum = 0;
    for (size_t i = 0; i < size; ++i) {
        sftm_arr[i] = std::exp(data[i] - *max_confidence);
        sum += sftm_arr.at(i);
    }
    if (sum > 0) {
        for (size_t i = 0; i < size; ++i) {
            sftm_arr[i] /= sum;
        }
    }
    auto max_elem = std::max_element(sftm_arr.begin(), sftm_arr.end());
    auto index = std::distance(sftm_arr.begin(), max_elem);
    result.set_string("label", labels.at(index));
    result.set_int("label_id", index);
    result.set_double("confidence", *max_elem);
}

// Function to select compound labels based on a confidence threshold.
template <typename DataType>
void compound_method(const DataType *data, size_t size, const std::vector<std::string> &labels, double threshold,
                     GVA::Tensor &result) {
    std::string result_label;
    double confidence = 0;
    for (size_t j = 0; j < size; j++) {
        std::string label;
        if (data[j] >= threshold) {
            label = labels.at(j * 2);
        } else if (data[j] > 0) {
            label = labels.at(j * 2 + 1);
        }
        if (!label.empty()) {
            if (!result_label.empty() and !isspace(result_label.back()))
                result_label += " ";
            result_label += label;
        }
        if (data[j] >= confidence)
            confidence = data[j];
    }
    result.set_string("label", result_label);
    result.set_double("confidence", confidence);
}

// Function to select multiple labels based on a confidence threshold.
template <typename DataType>
void multi_method(const DataType *data, size_t size, const std::vector<std::string> &labels, double threshold,
                  GVA::Tensor &result) {
    std::string result_label;
    double confidence = 0;
    for (size_t j = 0; j < size; j++) {
        std::string label;
        if (data[j] >= threshold) {
            label = labels.at(j);
        }
        if (!label.empty()) {
            if (!result_label.empty() and !isspace(result_label.back()))
                result_label += " ";
            result_label += label;
        }
        if (data[j] >= confidence)
            confidence = data[j];
    }
    result.set_string("label", result_label);
    result.set_double("confidence", confidence);
}

// Function to apply softmax and select multiple labels based on a confidence threshold.
template <typename DataType>
void softmax_multi_method(const DataType *data, size_t size, const std::vector<std::string> &labels, double threshold,
                          GVA::Tensor &result) {
    auto max_confidence = std::max_element(data, data + size);
    float *sftm_arr = new float[size];
    float sum = 0;
    for (size_t i = 0; i < size; ++i) {
        sftm_arr[i] = std::exp(data[i] - *max_confidence);
        sum += sftm_arr[i];
    }
    if (sum > 0) {
        for (size_t i = 0; i < size; ++i) {
            sftm_arr[i] /= sum;
        }
    }
    multi_method<float>(sftm_arr, size, labels, threshold, result);
    delete[] sftm_arr;
}

// Function to use integer indices from the data to select labels.
template <typename DataType>
void index_method([[maybe_unused]] const DataType *data, size_t size, const std::vector<std::string> &labels,
                  GVA::Tensor &result) {
    std::string result_label;
    int max_value = 0;
    for (size_t j = 0; j < size; j++) {
        int value = 0;
        if constexpr (std::is_same<int, DataType>::value)
            value = data[j];
        else
            value = safe_convert<int>(data[j]);
        if (value < 0 || static_cast<size_t>(value) >= labels.size())
            break;
        if (value > max_value)
            max_value = value;
        result_label += labels.at(value);
    }
    if (max_value) {
        result.set_string("label", result_label);
    }
}

// Function to map a string representation of a method to its corresponding enum value.
LabelConverter::Method method_from_string(const std::string &method_string) {
    const std::map<std::string, LabelConverter::Method> method_to_string_map = {
        {"max", LabelConverter::Method::Max},
        {"softmax", LabelConverter::Method::SoftMax},
        {"compound", LabelConverter::Method::Compound},
        {"multi", LabelConverter::Method::Multi},
        {"softmax_multi", LabelConverter::Method::SoftMaxMulti},
        {"index", LabelConverter::Method::Index}};

    const auto found = method_to_string_map.find(method_string);
    if (found == method_to_string_map.cend())
        return LabelConverter::Method::Default;

    return found->second;
}
} // namespace

// Constructor for LabelConverter, initializes the converter with configuration details.
LabelConverter::LabelConverter(BlobToMetaConverter::Initializer initializer)
    : BlobToTensorConverter(std::move(initializer)) {
    GstStructure *s = getModelProcOutputInfo().get();
    const auto method_string = gst_structure_get_string(s, "method");
    if (!method_string) {
        GVA_WARNING("Failed to get 'method' from model proc. Using default method");
        _method = LabelConverter::Method::Default;
    } else {
        _method = method_from_string(method_string);
    }
    gst_structure_get_double(s, "confidence_threshold", &_confidence_threshold);
}

// Template function to execute the selected post-processing method on the model's output data.
template <typename T>
void LabelConverter::ExecuteMethod(const T *data, const std::string &layer_name, InferenceBackend::OutputBlob::Ptr blob,
                                   TensorsTable &tensors_table) const {
    const auto &labels_raw = getLabels();
    const size_t batch_size = getModelInputImageInfo().batch_size;
    if (labels_raw.empty()) {
        throw std::invalid_argument("Failed to get list of classification labels.");
    }
    auto labels_raw_size = labels_raw.size();
    const size_t size = blob->GetSize();

    for (size_t frame_index = 0; frame_index < batch_size; ++frame_index) {
        GVA::Tensor classification_result = createTensor();

        if (!skipRawTensors())
            CopyOutputBlobToGstStructure(blob, classification_result.gst_structure(),
                                         BlobToMetaConverter::getModelName().c_str(), layer_name.c_str(), batch_size,
                                         frame_index);

        // FIXME: then c++17 avaliable
        const auto item = get_data_by_batch_index<T>(data, size, batch_size, frame_index);
        const T *item_data = item.first;
        const size_t item_data_size = item.second;

        if (_method != Method::Index && labels_raw_size > (_method == Method::Compound ? 2 : 1) * item_data_size) {
            throw std::invalid_argument("Wrong number of classification labels.");
        }

        switch (_method) {
        case Method::SoftMax:
            soft_max_method<T>(item_data, item_data_size, labels_raw, classification_result);
            break;
        case Method::Compound:
            compound_method<T>(item_data, item_data_size, labels_raw, _confidence_threshold, classification_result);
            break;
        case Method::Multi:
            multi_method<T>(item_data, item_data_size, labels_raw, _confidence_threshold, classification_result);
            break;
        case Method::SoftMaxMulti:
            softmax_multi_method<T>(item_data, item_data_size, labels_raw, _confidence_threshold,
                                    classification_result);
            break;
        case Method::Index:
            index_method<T>(item_data, item_data_size, labels_raw, classification_result);
            break;
        case Method::Max:
            max_method<T>(item_data, item_data_size, labels_raw, classification_result);
            break;
        default:
            throw std::runtime_error("Unknown method for 'to label' converter");
        }

        /* tensor_id - In different GStreamer versions tensors_batch are attached to the buffer in a different
         * order. */
        /* type - To identify classification tensors among others. */
        /* element_id - To identify model_instance_id. */
        gst_structure_set(classification_result.gst_structure(), "tensor_id", G_TYPE_INT,
                          safe_convert<int>(frame_index), "type", G_TYPE_STRING, "classification_result", NULL);
        std::vector<GstStructure *> tensors{classification_result.gst_structure()};
        tensors_table[frame_index].push_back(tensors);
    }
}

// Function to convert output blobs from the model into tensors that can be used by GStreamer.
TensorsTable LabelConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    TensorsTable tensors_table;

    const size_t batch_size = getModelInputImageInfo().batch_size;
    tensors_table.resize(batch_size);

    for (const auto &blob_iter : output_blobs) {
        const std::string layer_name = blob_iter.first;
        OutputBlob::Ptr blob = blob_iter.second;
        if (not blob) {
            throw std::invalid_argument("Output blob is empty.");
        }

        if (blob->GetData() == nullptr)
            throw std::invalid_argument("Output blob data is nullptr");

        // Determine the data type and execute the appropriate method.
        if (blob->GetPrecision() == InferenceBackend::Blob::Precision::FP32)
            ExecuteMethod<float>(reinterpret_cast<const float *>(blob->GetData()), layer_name, blob, tensors_table);
        else if (blob->GetPrecision() == InferenceBackend::Blob::Precision::FP64)
            ExecuteMethod<double>(reinterpret_cast<const double *>(blob->GetData()), layer_name, blob, tensors_table);
        else if (blob->GetPrecision() == InferenceBackend::Blob::Precision::I32)
            ExecuteMethod<int>(reinterpret_cast<const int *>(blob->GetData()), layer_name, blob, tensors_table);
        else
            throw std::runtime_error("Unsupported data type");
    }

    return tensors_table;
}