/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "zeroshot_openclip.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"
#include "safetensors_reader.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace post_processing {
namespace {

const std::vector<std::string> kEmbeddingTensorNames = {"embeddings", "label_embeddings", "E_text",
                                                        "text_embeddings"};
constexpr const char *kUnknownLabel = "unknown";

std::vector<float> normalizeVector(const float *src, std::size_t size) {
    if (size == 0)
        throw std::invalid_argument("Embedding vector size is zero.");

    double norm_sq = 0.0;
    for (std::size_t i = 0; i < size; ++i)
        norm_sq += static_cast<double>(src[i]) * static_cast<double>(src[i]);

    const double norm = std::sqrt(norm_sq);
    if (norm <= std::numeric_limits<double>::epsilon())
        throw std::runtime_error("Embedding vector norm is zero.");

    std::vector<float> normalized(size);
    for (std::size_t i = 0; i < size; ++i)
        normalized[i] = static_cast<float>(src[i] / norm);

    return normalized;
}

} // namespace

ZeroShotOpenCLIPConverter::ZeroShotOpenCLIPConverter(BlobToMetaConverter::Initializer initializer)
    : BlobToTensorConverter(std::move(initializer)) {
    topk_ = std::max<uint32_t>(1, getZeroshotTopk());

    const std::string &embeddings_path = getZeroshotEmbeddingsFile();
    if (embeddings_path.empty())
        throw std::invalid_argument("Zero-shot embeddings file is not set. Use gvaclassify zeroshot-embeddings-file.");

    loadEmbeddings(embeddings_path);

    const auto &labels = getLabels();
    if (!labels.empty() && labels.size() != num_classes_) {
        throw std::invalid_argument("Number of labels (" + std::to_string(labels.size()) +
                                    ") does not match number of embeddings (" + std::to_string(num_classes_) + ").");
    }

    if (logit_scale_ <= 0.0f) {
        logit_scale_ = 1.0f;
        GVA_WARNING("Zero-shot: no logit_scale found in the embeddings file metadata; using 1.0. "
                    "Class ranking is unaffected, but the reported confidences will be uncalibrated (flat). "
                    "Store the model's CLIP logit_scale in the embeddings file to obtain calibrated probabilities.");
    }

    GVA_INFO("Zero-shot OpenCLIP: %zu classes, embedding_dim=%zu, top-k=%u, logit_scale=%.3f, unknown_threshold=%s.",
             num_classes_, embedding_dim_, topk_, static_cast<double>(logit_scale_),
             unknown_threshold_ >= 0.0 ? std::to_string(unknown_threshold_).c_str() : "disabled");
}

void ZeroShotOpenCLIPConverter::loadEmbeddings(const std::string &embeddings_path) {
    SafetensorsMatrix matrix;
    try {
        matrix = load_safetensors_matrix(embeddings_path, kEmbeddingTensorNames);
    } catch (const std::exception &e) {
        throw std::runtime_error("Failed to load zero-shot embeddings from '" + embeddings_path + "': " + e.what());
    }

    num_classes_ = matrix.rows;
    embedding_dim_ = matrix.cols;
    embeddings_ = std::move(matrix.data);

    // Defensive L2-normalization of each class prototype (cheap; tolerates un-normalized inputs).
    for (std::size_t row = 0; row < num_classes_; ++row) {
        float *row_ptr = embeddings_.data() + row * embedding_dim_;
        const auto normalized = normalizeVector(row_ptr, embedding_dim_);
        std::copy(normalized.begin(), normalized.end(), row_ptr);
    }

    const auto it = matrix.metadata.find("logit_scale");
    if (it != matrix.metadata.end()) {
        try {
            logit_scale_ = std::stof(it->second);
        } catch (const std::exception &) {
            GVA_WARNING("Zero-shot: could not parse logit_scale metadata value '%s'.", it->second.c_str());
        }
    }

    // Optional rejection threshold carried in the embeddings file metadata: if the top-1 cosine
    // similarity is below it, the result is labelled "unknown" (label_id -1). Negative disables it.
    const auto ut = matrix.metadata.find("unknown_threshold");
    if (ut != matrix.metadata.end()) {
        try {
            unknown_threshold_ = std::stod(ut->second);
        } catch (const std::exception &) {
            GVA_WARNING("Zero-shot: could not parse unknown_threshold metadata value '%s'.", ut->second.c_str());
        }
    }
}

std::vector<float> ZeroShotOpenCLIPConverter::computeSimilarities(const float *image_embedding,
                                                                  std::size_t image_embedding_size) const {
    if (image_embedding_size < embedding_dim_) {
        throw std::runtime_error("Image embedding dimension " + std::to_string(image_embedding_size) +
                                 " is smaller than label embedding dimension " + std::to_string(embedding_dim_) + ".");
    }
    if (image_embedding_size > embedding_dim_) {
        throw std::runtime_error("Image embedding dimension " + std::to_string(image_embedding_size) +
                                 " does not match label embedding dimension " + std::to_string(embedding_dim_) +
                                 ". Re-export the vision model and embeddings with the same CLIP model.");
    }

    const auto normalized_image = normalizeVector(image_embedding, embedding_dim_);
    std::vector<float> similarities(num_classes_, 0.0F);

    for (std::size_t row = 0; row < num_classes_; ++row) {
        const float *class_embedding = embeddings_.data() + row * embedding_dim_;
        float dot = 0.0F;
        for (std::size_t col = 0; col < embedding_dim_; ++col)
            dot += normalized_image[col] * class_embedding[col];
        similarities[row] = dot;
    }

    return similarities;
}

std::vector<float> ZeroShotOpenCLIPConverter::computeProbabilities(const std::vector<float> &similarities) const {
    if (similarities.empty())
        throw std::runtime_error("No similarities produced for zero-shot classification.");

    // CLIP scores are logit_scale * cosine_similarity; softmax over those gives calibrated probabilities.
    std::vector<float> logits(similarities.size());
    for (std::size_t i = 0; i < similarities.size(); ++i)
        logits[i] = logit_scale_ * similarities[i];

    const float max_logit = *std::max_element(logits.begin(), logits.end());

    std::vector<float> probabilities(logits.size(), 0.0F);
    double sum = 0.0;
    for (std::size_t i = 0; i < logits.size(); ++i) {
        probabilities[i] = std::exp(logits[i] - max_logit);
        sum += probabilities[i];
    }

    if (sum <= std::numeric_limits<double>::epsilon())
        throw std::runtime_error("Softmax normalization sum is zero.");

    for (float &probability : probabilities)
        probability = static_cast<float>(probability / sum);

    return probabilities;
}

TensorsTable ZeroShotOpenCLIPConverter::convert(const OutputBlobs &output_blobs) {
    if (output_blobs.empty())
        throw std::invalid_argument("Output blobs are empty.");
    if (output_blobs.size() != 1) {
        throw std::runtime_error("Zero-shot converter expects exactly one output blob, got " +
                                 std::to_string(output_blobs.size()) + ".");
    }

    const auto &blob_it = *output_blobs.begin();
    const std::string &layer_name = blob_it.first;
    InferenceBackend::OutputBlob::Ptr blob = blob_it.second;
    if (!blob)
        throw std::invalid_argument("Output blob is empty.");
    if (blob->GetData() == nullptr)
        throw std::invalid_argument("Output blob data is nullptr.");

    const size_t batch_size = getModelInputImageInfo().batch_size;
    TensorsTable tensors_table(batch_size);

    std::vector<float> blob_data_fp32;
    const float *blob_data = nullptr;
    if (blob->GetPrecision() == InferenceBackend::Blob::Precision::FP32) {
        blob_data = reinterpret_cast<const float *>(blob->GetData());
    } else if (blob->GetPrecision() == InferenceBackend::Blob::Precision::FP64) {
        const auto *blob_data_fp64 = reinterpret_cast<const double *>(blob->GetData());
        blob_data_fp32.resize(blob->GetSize());
        for (size_t i = 0; i < blob->GetSize(); ++i)
            blob_data_fp32[i] = static_cast<float>(blob_data_fp64[i]);
        blob_data = blob_data_fp32.data();
    } else {
        throw std::runtime_error("Zero-shot converter supports FP32/FP64 output only.");
    }

    const auto &labels = getLabels();
    const uint32_t actual_topk = static_cast<uint32_t>(std::min<size_t>(topk_, num_classes_));
    const std::string &model_name = BlobToMetaConverter::getModelName();

    for (size_t frame_index = 0; frame_index < batch_size; ++frame_index) {
        const auto item = get_data_by_batch_index<float>(blob_data, blob->GetSize(), batch_size, frame_index);
        const float *item_data = item.first;
        const size_t item_data_size = item.second;

        const auto similarities = computeSimilarities(item_data, item_data_size);
        const auto probabilities = computeProbabilities(similarities);

        std::vector<size_t> indices(num_classes_);
        std::iota(indices.begin(), indices.end(), 0);
        std::partial_sort(indices.begin(), indices.begin() + actual_topk, indices.end(),
                          [&](size_t lhs, size_t rhs) { return similarities[lhs] > similarities[rhs]; });

        const size_t top_class = indices.front();
        const bool is_unknown = (unknown_threshold_ >= 0.0) &&
                                (static_cast<double>(similarities[top_class]) < unknown_threshold_);
        // When the best match is too weak, emit a single "unknown" result instead of the ranking.
        const uint32_t emitted = is_unknown ? 1u : actual_topk;

        for (uint32_t rank = 0; rank < emitted; ++rank) {
            const size_t class_id = indices[rank];
            GVA::Tensor classification_result = createTensor();

            if (!skipRawTensors() && rank == 0) {
                CopyOutputBlobToGstStructure(blob, classification_result.gst_structure(), model_name.c_str(),
                                             layer_name.c_str(), batch_size, frame_index);
            }

            if (is_unknown) {
                classification_result.set_string("label", kUnknownLabel);
                classification_result.set_int("label_id", -1);
            } else {
                const std::string label = labels.empty() ? std::to_string(class_id) : labels.at(class_id);
                classification_result.set_string("label", label);
                classification_result.set_int("label_id", safe_convert<int>(class_id));
            }
            classification_result.set_double("confidence", probabilities[class_id]);
            classification_result.set_int("rank", rank + 1);

            gst_structure_set(classification_result.gst_structure(), "tensor_id", G_TYPE_INT,
                              safe_convert<int>(frame_index), "type", G_TYPE_STRING, GVA::GST_ANALYTICS_CLS_2_TENSOR,
                              "zs_mode", G_TYPE_BOOLEAN, TRUE, "zs_unknown", G_TYPE_BOOLEAN,
                              is_unknown ? TRUE : FALSE, "zs_model", G_TYPE_STRING, model_name.c_str(), NULL);

            std::vector<GstStructure *> tensor_entry{classification_result.gst_structure()};
            tensors_table[frame_index].push_back(std::move(tensor_entry));
        }
    }

    return tensors_table;
}

} // namespace post_processing
