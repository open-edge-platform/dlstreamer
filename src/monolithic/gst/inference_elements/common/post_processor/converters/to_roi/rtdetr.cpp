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

std::vector<float> softmax(const float *data, size_t size) {
    std::vector<float> probs(size);
    float max_val = data[0];
    for (size_t i = 1; i < size; ++i)
        max_val = std::max(max_val, data[i]);

    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        probs[i] = std::exp(data[i] - max_val);
        sum += probs[i];
    }

    if (sum <= 0.0f)
        return probs;

    for (size_t i = 0; i < size; ++i)
        probs[i] /= sum;

    return probs;
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

    const bool has_no_object = logits_classes > labels_count;
    const size_t valid_classes = has_no_object ? labels_count : std::min(labels_count, logits_classes);
    if (valid_classes == 0)
        throw std::invalid_argument("No valid classes for RT-DETR post-processing.");

    const auto &model_input_image_info = getModelInputImageInfo();
    const float input_width = static_cast<float>(model_input_image_info.width);
    const float input_height = static_cast<float>(model_input_image_info.height);

    for (size_t i = 0; i < num_queries; ++i) {
        const float *logits = logits_data + i * logits_classes;
        const float *box = boxes_data + i * boxes_object_size;

        auto probs = softmax(logits, logits_classes);

        size_t best_class = 0;
        float best_score = probs[0];
        for (size_t c = 1; c < valid_classes; ++c) {
            if (probs[c] > best_score) {
                best_score = probs[c];
                best_class = c;
            }
        }

        if (best_score < confidence_threshold)
            continue;

        const float cx = box[0];
        const float cy = box[1];
        const float w = box[2];
        const float h = box[3];

        const float x1 = (cx - 0.5f * w) * input_width;
        const float y1 = (cy - 0.5f * h) * input_height;
        const float bw = w * input_width;
        const float bh = h * input_height;

        objects.emplace_back(x1, y1, bw, bh, 0.0f, best_score, best_class,
                             BlobToMetaConverter::getLabelByLabelId(best_class), 1.0f / input_width,
                             1.0f / input_height, false);
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