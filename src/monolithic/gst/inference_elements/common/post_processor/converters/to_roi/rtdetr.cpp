/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "rtdetr.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"

#include <gst/gst.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace post_processing;

namespace {

size_t getNumQueriesFromDims(const std::vector<size_t> &dims) {
    if (dims.size() < BlobToROIConverter::min_dims_size)
        throw std::invalid_argument("Output blob dimensions size " + std::to_string(dims.size()) +
                                    " is not supported (less than " +
                                    std::to_string(BlobToROIConverter::min_dims_size) + ").");
    return dims[dims.size() - 2];
}

float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

} // namespace

void RTDETRConverter::parseOutputBlobs(const float *logits_data, const std::vector<size_t> &logits_dims,
                                       const float *boxes_data, const std::vector<size_t> &boxes_dims,
                                       std::vector<DetectedObject> &objects) const {
    if (!logits_data || !boxes_data)
        throw std::invalid_argument("Output blob data is nullptr.");

    const size_t boxes_object_size = boxes_dims.back();
    if (boxes_object_size != 4)
        throw std::invalid_argument("RT-DETR boxes output should have 4 values per box.");

    const size_t num_queries = getNumQueriesFromDims(boxes_dims);
    const size_t logits_num_queries = getNumQueriesFromDims(logits_dims);
    if (num_queries != logits_num_queries)
        throw std::invalid_argument("RT-DETR logits and boxes have different query counts.");

    const size_t logits_classes = logits_dims.back();
    if (logits_classes == 0)
        throw std::invalid_argument("RT-DETR logits output has zero classes.");

    const size_t labels_count = BlobToMetaConverter::getLabels().size();
    if (labels_count == 0)
        throw std::invalid_argument("Num classes is zero.");

    const auto &model_input_image_info = getModelInputImageInfo();
    const float input_width = static_cast<float>(model_input_image_info.width);
    const float input_height = static_cast<float>(model_input_image_info.height);

    // Like transformers: sigmoid per class, then the num_queries best (query, class) pairs over the threshold.
    const size_t valid_classes = std::min(labels_count, logits_classes);
    std::vector<std::tuple<float, size_t, size_t>> candidates;
    for (size_t i = 0; i < num_queries; ++i) {
        const float *logits = logits_data + i * logits_classes;
        for (size_t c = 0; c < valid_classes; ++c) {
            const float score = sigmoid(logits[c]);
            if (score >= confidence_threshold)
                candidates.emplace_back(score, i, c);
        }
    }
    const size_t keep = std::min(candidates.size(), num_queries);
    std::partial_sort(candidates.begin(), candidates.begin() + keep, candidates.end(),
                      [](const auto &a, const auto &b) { return std::get<0>(a) > std::get<0>(b); });

    for (size_t k = 0; k < keep; ++k) {
        const auto [score, query, class_id] = candidates[k];
        const float *box = boxes_data + query * boxes_object_size;
        const float cx = box[0];
        const float cy = box[1];
        const float w = box[2];
        const float h = box[3];
        objects.emplace_back((cx - 0.5f * w) * input_width, (cy - 0.5f * h) * input_height, w * input_width,
                             h * input_height, 0.0f, score, class_id, BlobToMetaConverter::getLabelByLabelId(class_id),
                             1.0f / input_width, 1.0f / input_height, false);
    }
}

TensorsTable RTDETRConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        const size_t batch_size = model_input_image_info.batch_size;

        InferenceBackend::OutputBlob::Ptr logits_blob = nullptr;
        InferenceBackend::OutputBlob::Ptr boxes_blob = nullptr;

        for (const auto &blob_iter : output_blobs) {
            const InferenceBackend::OutputBlob::Ptr &blob = blob_iter.second;
            if (!blob)
                throw std::invalid_argument("Output blob is nullptr.");

            const auto &dims = blob->GetDims();
            if (dims.size() < BlobToROIConverter::min_dims_size)
                continue;

            if (dims.back() == 4)
                boxes_blob = blob;
            else if (dims.back() > 4)
                logits_blob = blob;
        }

        if (!logits_blob || !boxes_blob) {
            std::throw_with_nested(std::runtime_error("Failed to identify output blobs for RT-DETR converter."));
        }

        if (logits_blob->GetPrecision() != InferenceBackend::Blob::Precision::FP32 ||
            boxes_blob->GetPrecision() != InferenceBackend::Blob::Precision::FP32) {
            throw std::runtime_error("Unsupported RT-DETR output precision (expected FP32).");
        }

        DetectedObjectsTable objects_table(batch_size);

        for (size_t batch_number = 0; batch_number < batch_size; ++batch_number) {
            auto &objects = objects_table[batch_number];

            const size_t logits_unbatched_size = logits_blob->GetSize() / batch_size;
            const size_t boxes_unbatched_size = boxes_blob->GetSize() / batch_size;

            parseOutputBlobs(
                reinterpret_cast<const float *>(logits_blob->GetData()) + logits_unbatched_size * batch_number,
                logits_blob->GetDims(),
                reinterpret_cast<const float *>(boxes_blob->GetData()) + boxes_unbatched_size * batch_number,
                boxes_blob->GetDims(), objects);
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do RT-DETR post-processing."));
    }
    return TensorsTable{};
}