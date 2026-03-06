/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "boxes_labels.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace post_processing;

const std::string BoxesLabelsConverter::labels_layer_name = "labels";

InferenceBackend::OutputBlob::Ptr BoxesLabelsConverter::getLabelsScoresBlob(const OutputBlobs &output_blobs) const {
    return output_blobs.at(labels_layer_name);
}

std::pair<size_t, float>
BoxesLabelsConverter::getLabelIdConfidence(const InferenceBackend::OutputBlob::Ptr &labels_blob, size_t bbox_i,
                                           float conf) const {
    if (!labels_blob)
        throw std::runtime_error("Output blob is nullptr.");
    if (!labels_blob->GetData())
        throw std::runtime_error("Output blob data is nullptr.");

    const float *labels_data_fp32;
    const uint32_t *labels_data32;
    const uint64_t *labels_data64;

    switch (labels_blob->GetPrecision()) {
    case InferenceBackend::Blob::Precision::FP32:
        labels_data_fp32 = reinterpret_cast<const float *>(labels_blob->GetData());
        return std::make_pair(safe_convert<size_t>(static_cast<uint32_t>(labels_data_fp32[bbox_i])), conf);
    case InferenceBackend::Blob::Precision::U32:
    case InferenceBackend::Blob::Precision::I32:
        labels_data32 = reinterpret_cast<const uint32_t *>(labels_blob->GetData());
        return std::make_pair(safe_convert<size_t>(labels_data32[bbox_i]), conf);
    case InferenceBackend::Blob::Precision::U64:
    case InferenceBackend::Blob::Precision::I64:
        labels_data64 = reinterpret_cast<const uint64_t *>(labels_blob->GetData());
        return std::make_pair(safe_convert<size_t>(safe_convert<uint32_t>(labels_data64[bbox_i])), conf);
    default:
        throw std::runtime_error("Unsupported label precision.");
    }
}

bool BoxesLabelsConverter::isValidModelOutputs(const std::map<std::string, std::vector<size_t>> &model_outputs_info) {
    if (!BoxesLabelsScoresConverter::isValidModelBoxesOutput(model_outputs_info))
        return false;

    return BoxesLabelsScoresConverter::isValidModelAdditionalOutput(model_outputs_info, labels_layer_name);
}
