/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "rfdetr.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <gst/gst.h>

#include <algorithm>
#include <cmath>
#include <cstring>
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

inline float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

} // namespace

void RFDETRConverter::parseOutputBlobs(const float *logits_data, const std::vector<size_t> &logits_dims,
                                       const float *boxes_data, const std::vector<size_t> &boxes_dims,
                                       std::vector<DetectedObject> &objects) const {
    if (!logits_data || !boxes_data)
        throw std::invalid_argument("Output blob data is nullptr.");

    const size_t boxes_object_size = boxes_dims.back();
    if (boxes_object_size != 4)
        throw std::invalid_argument("RF-DETR boxes output should have 4 values per box.");

    const size_t num_queries = getNumQueriesFromDims(boxes_dims);
    const size_t logits_num_queries = getNumQueriesFromDims(logits_dims);
    if (num_queries != logits_num_queries)
        throw std::invalid_argument("RF-DETR logits and boxes have different query counts.");

    const size_t logits_classes = logits_dims.back();
    if (logits_classes == 0)
        throw std::invalid_argument("RF-DETR logits output has zero classes.");

    const size_t labels_count = BlobToMetaConverter::getLabels().size();
    if (labels_count == 0)
        throw std::invalid_argument("Num classes is zero.");

    const size_t valid_classes = std::min(labels_count, logits_classes);
    if (valid_classes < 1)
        throw std::invalid_argument("No valid classes for RF-DETR post-processing.");

    const auto &model_input_image_info = getModelInputImageInfo();
    const float input_width = static_cast<float>(model_input_image_info.width);
    const float input_height = static_cast<float>(model_input_image_info.height);

    for (size_t i = 0; i < num_queries; ++i) {
        const float *logits = logits_data + i * logits_classes;
        const float *box = boxes_data + i * boxes_object_size;

        // RF-DETR: per-class sigmoid, no background class (model rt_info: labels.activation = "sigmoid")
        size_t best_class = 0;
        float best_score = sigmoid(logits[0]);
        for (size_t c = 1; c < valid_classes; ++c) {
            float score = sigmoid(logits[c]);
            if (score > best_score) {
                best_score = score;
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

TensorsTable RFDETRConverter::convert(const OutputBlobs &output_blobs) {
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
            throw std::runtime_error("Failed to identify output blobs for RF-DETR converter.");
        }

        if (logits_blob->GetPrecision() != InferenceBackend::Blob::Precision::FP32 ||
            boxes_blob->GetPrecision() != InferenceBackend::Blob::Precision::FP32) {
            throw std::runtime_error("Unsupported RF-DETR output precision (expected FP32).");
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
        std::throw_with_nested(std::runtime_error("Failed to do RF-DETR post-processing."));
    }
    return TensorsTable{};
}

TensorsTable RFDETRSegConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        const size_t batch_size = model_input_image_info.batch_size;
        const float input_width = static_cast<float>(model_input_image_info.width);
        const float input_height = static_cast<float>(model_input_image_info.height);

        InferenceBackend::OutputBlob::Ptr logits_blob = nullptr;
        InferenceBackend::OutputBlob::Ptr boxes_blob = nullptr;
        InferenceBackend::OutputBlob::Ptr masks_blob = nullptr;

        for (const auto &blob_iter : output_blobs) {
            const InferenceBackend::OutputBlob::Ptr &blob = blob_iter.second;
            if (!blob)
                throw std::invalid_argument("Output blob is nullptr.");

            const auto &dims = blob->GetDims();
            if (dims.size() < BlobToROIConverter::min_dims_size)
                continue;

            if (dims.size() == 3 && dims.back() == 4) {
                boxes_blob = blob;
            } else if (dims.size() == 3 && dims.back() > 4) {
                logits_blob = blob;
            } else if (dims.size() == 4) {
                masks_blob = blob;
            }
        }

        if (!logits_blob || !boxes_blob) {
            throw std::runtime_error("Failed to identify output blobs for RF-DETR-Seg converter.");
        }

        if (logits_blob->GetPrecision() != InferenceBackend::Blob::Precision::FP32 ||
            boxes_blob->GetPrecision() != InferenceBackend::Blob::Precision::FP32) {
            throw std::runtime_error("Unsupported RF-DETR-Seg output precision (expected FP32).");
        }

        const auto &logits_dims = logits_blob->GetDims();

        const size_t num_queries = logits_dims[logits_dims.size() - 2];
        const size_t logits_classes = logits_dims.back();
        const size_t labels_count = BlobToMetaConverter::getLabels().size();
        const size_t valid_classes = std::min(labels_count, logits_classes);

        if (valid_classes < 1)
            throw std::invalid_argument("No valid classes for RF-DETR-Seg post-processing.");

        size_t masks_height = 0;
        size_t masks_width = 0;
        size_t mask_stride = 0;
        if (masks_blob) {
            const auto &masks_dims = masks_blob->GetDims();
            masks_height = masks_dims[masks_dims.size() - 2];
            masks_width = masks_dims[masks_dims.size() - 1];
            mask_stride = masks_height * masks_width;
        }

        DetectedObjectsTable objects_table(batch_size);

        for (size_t batch_number = 0; batch_number < batch_size; ++batch_number) {
            auto &objects = objects_table[batch_number];

            const size_t logits_unbatched_size = logits_blob->GetSize() / batch_size;
            const size_t boxes_unbatched_size = boxes_blob->GetSize() / batch_size;

            const float *logits_data =
                reinterpret_cast<const float *>(logits_blob->GetData()) + logits_unbatched_size * batch_number;
            const float *boxes_data =
                reinterpret_cast<const float *>(boxes_blob->GetData()) + boxes_unbatched_size * batch_number;

            const float *masks_data = nullptr;
            if (masks_blob) {
                const size_t masks_unbatched_size = masks_blob->GetSize() / batch_size;
                masks_data =
                    reinterpret_cast<const float *>(masks_blob->GetData()) + masks_unbatched_size * batch_number;
            }

            for (size_t i = 0; i < num_queries; ++i) {
                const float *logits = logits_data + i * logits_classes;
                const float *box = boxes_data + i * 4;

                size_t best_class = 0;
                float best_score = sigmoid(logits[0]);
                for (size_t c = 1; c < valid_classes; ++c) {
                    float score = sigmoid(logits[c]);
                    if (score > best_score) {
                        best_score = score;
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

                auto detected_object = DetectedObject(x1, y1, bw, bh, 0.0f, best_score, best_class,
                                                      BlobToMetaConverter::getLabelByLabelId(best_class),
                                                      1.0f / input_width, 1.0f / input_height, false);

                if (masks_data) {
                    const float *mask = masks_data + i * mask_stride;

                    int mx1 = std::max(0, static_cast<int>(std::floor((cx - 0.5f * w) * masks_width)));
                    int my1 = std::max(0, static_cast<int>(std::floor((cy - 0.5f * h) * masks_height)));
                    int mx2 = std::min(static_cast<int>(masks_width),
                                       static_cast<int>(std::ceil((cx + 0.5f * w) * masks_width)));
                    int my2 = std::min(static_cast<int>(masks_height),
                                       static_cast<int>(std::ceil((cy + 0.5f * h) * masks_height)));
                    int crop_w = std::max(mx2 - mx1, 1);
                    int crop_h = std::max(my2 - my1, 1);
                    size_t crop_size = static_cast<size_t>(crop_w) * crop_h;

                    std::vector<float> cropped(crop_size);
                    for (int row = 0; row < crop_h; ++row) {
                        const float *src = &mask[(my1 + row) * masks_width + mx1];
                        float *dst = &cropped[row * crop_w];
                        for (int col = 0; col < crop_w; ++col)
                            dst[col] = sigmoid(src[col]);
                    }

                    GstStructure *tensor = gst_structure_copy(getModelProcOutputInfo().get());
                    gst_structure_set_name(tensor, "rfdetr_seg");
                    gst_structure_set(tensor, "precision", G_TYPE_INT, GVA_PRECISION_FP32, NULL);
                    gst_structure_set(tensor, "format", G_TYPE_STRING, GVA::TENSOR_FORMAT_INSTANCE_SEGMENTATION, NULL);
                    gst_structure_set(tensor, "type", G_TYPE_STRING, GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR, NULL);

                    GValueArray *data = g_value_array_new(2);
                    GValue gvalue = G_VALUE_INIT;
                    g_value_init(&gvalue, G_TYPE_UINT);
                    g_value_set_uint(&gvalue, safe_convert<uint32_t>(crop_w));
                    g_value_array_append(data, &gvalue);
                    g_value_set_uint(&gvalue, safe_convert<uint32_t>(crop_h));
                    g_value_array_append(data, &gvalue);
                    gst_structure_set_array(tensor, "dims", data);
                    g_value_array_free(data);

                    copy_buffer_to_structure(tensor, reinterpret_cast<const void *>(cropped.data()),
                                             crop_size * sizeof(float));
                    detected_object.tensors.push_back(tensor);
                }

                objects.push_back(detected_object);
            }
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do RF-DETR-Seg post-processing."));
    }
    return TensorsTable{};
}