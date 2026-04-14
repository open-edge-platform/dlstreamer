/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "paddle_ocr.h"
#include "copy_blob_to_gststruct.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <gst/gst.h>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace post_processing;
using namespace InferenceBackend;

// Constructor to initialize the OCRConverter with the initializer.
PaddleOCRConverter::PaddleOCRConverter(BlobToMetaConverter::Initializer initializer)
    : BlobToTensorConverter(std::move(initializer)) {
}

TensorsTable PaddleOCRConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    TensorsTable tensors_table;

    try {
        const size_t batch_size = getModelInputImageInfo().batch_size;
        tensors_table.resize(batch_size);

        for (const auto &blob_iter : output_blobs) {
            OutputBlob::Ptr blob = blob_iter.second;
            if (!blob) {
                throw std::invalid_argument("Output blob is empty");
            }

            const float *data = reinterpret_cast<const float *>(blob->GetData());
            if (!data) {
                throw std::invalid_argument("Output blob data is nullptr");
            }

            const size_t data_size = blob->GetSize();
            const std::string layer_name = blob_iter.first;

            for (size_t batch_elem_index = 0; batch_elem_index < batch_size; ++batch_elem_index) {
                GVA::Tensor classification_result = createTensor();

                if (!raw_tensor_copying->enabled(RawTensorCopyingToggle::id))
                    CopyOutputBlobToGstStructure(blob, classification_result.gst_structure(),
                                                 BlobToMetaConverter::getModelName().c_str(), layer_name.c_str(),
                                                 batch_size, batch_elem_index);

                const auto item = get_data_by_batch_index(data, data_size, batch_size, batch_elem_index);
                const float *item_data = item.first;

                std::string decoded_text = decodeOutputTensor(item_data);

                if (decoded_text.size() > SEQ_MINLEN)
                    classification_result.set_string("label", decoded_text);
                else
                    classification_result.set_string("label", "");

                // Set metadata for the tensor in the GstStructure
                gst_structure_set(classification_result.gst_structure(), "tensor_id", G_TYPE_INT,
                                  safe_convert<int>(batch_elem_index), "type", G_TYPE_STRING, "classification_result",
                                  NULL);
                std::vector<GstStructure *> tensors{classification_result.gst_structure()};
                tensors_table[batch_elem_index].push_back(tensors);
            }
        }
    } catch (const std::exception &e) {
        GVA_ERROR("An error occurred in OCR converter: %s", e.what());
    }

    return tensors_table;
}

// Function to decode output tensor into text using the charset
std::string PaddleOCRConverter::decodeOutputTensor(const float *item_data) {

    std::vector<int> pred_indices(SEQUENCE_LENGTH); // Stores indices of max elements for each sequence

    for (size_t i = 0; i < SEQUENCE_LENGTH; ++i) {
        const float *row_start = item_data + i * CHARSET_LEN; // Pointer to the start of the current sequence
        const float *max_element_ptr = std::max_element(row_start, row_start + CHARSET_LEN); // Find max element
        int max_index = std::distance(row_start, max_element_ptr); // Calculate index of max element
        pred_indices[i] = max_index;                               // Store the index
    }

    // Decode the indices into text using the charset
    return decode(pred_indices);
}

// Function to decode text indices into text labels using a charset
std::string PaddleOCRConverter::decode(const std::vector<int> &text_index) {

    std::string char_list;                 // Accumulates characters for the sequence
    std::vector<int> ignored_tokens = {0}; // Tokens to ignore during decoding

    // Iterate over each index in the sequence
    for (size_t idx = 0; idx < text_index.size(); ++idx) {
        int current_index = text_index[idx];

        // Skip ignored tokens
        if (std::find(ignored_tokens.begin(), ignored_tokens.end(), current_index) != ignored_tokens.end()) {
            continue;
        }

        // Remove consecutive duplicate indices (optional)
        if (idx > 0 && text_index[idx - 1] == current_index) {
            continue;
        }

        if (current_index >= 0 && current_index < (int)CHARACTER_SET.size()) {
            char_list.append(CHARACTER_SET[current_index]);
        }
    }

    return char_list; // Return the decoded text
}

// ==================== PaddleOCRCtcConverter ====================

PaddleOCRCtcConverter::PaddleOCRCtcConverter(BlobToMetaConverter::Initializer initializer)
    : BlobToTensorConverter(std::move(initializer)) {
    loadVocabularyFromModelProc();
}

void PaddleOCRCtcConverter::loadVocabularyFromModelProc() {
    GstStructure *s = getModelProcOutputInfo().get();
    if (!s) {
        GVA_WARNING("PaddleOCR CTC converter: model_proc_output_info is null — using empty vocabulary");
        return;
    }

    const GValue *dict_value = gst_structure_get_value(s, "character_dict");
    if (!dict_value || !GST_VALUE_HOLDS_ARRAY(dict_value)) {
        GVA_WARNING("PaddleOCR CTC converter: character_dict not found in model_proc_output_info");
        return;
    }

    guint n = gst_value_array_get_size(dict_value);
    vocabulary.reserve(n);
    for (guint i = 0; i < n; ++i) {
        const GValue *item = gst_value_array_get_value(dict_value, i);
        if (G_VALUE_HOLDS_STRING(item)) {
            vocabulary.push_back(g_value_get_string(item));
        }
    }
    GVA_INFO("Loaded PaddleOCR character dictionary: %zu characters from model metadata", vocabulary.size());
}

std::pair<std::string, double> PaddleOCRCtcConverter::ctcDecode(const float *data, size_t seq_len, size_t vocab_size) {
    std::string result;
    std::vector<float> confidences;
    int prev_idx = 0;

    for (size_t t = 0; t < seq_len; ++t) {
        // find index of maximum confidence logit within the current sequence step
        const float *row = data + t * vocab_size;
        int max_idx = static_cast<int>(std::max_element(row, row + vocab_size) - row);

        // Element 0 is CTC blank and indicates entire sequence should be skipped
        // If current index matches previous index, we also skip it to avoid duplicates
        if (max_idx == 0 || max_idx == prev_idx) {
            prev_idx = max_idx;
            continue;
        }
        prev_idx = max_idx;

        // Convert element index to Vocabulary character index
        // Vocabulary is 1-based indexed, so subtract 1
        size_t char_idx = static_cast<size_t>(max_idx - 1);
        if (char_idx >= vocabulary.size())
            continue;

        // Add new character to output label
        result.append(vocabulary[char_idx]);
        confidences.push_back(row[max_idx]);
    }

    // return mean of character confidences as overall confidence score
    double confidence = 0.0;
    if (!confidences.empty()) {
        confidence = std::accumulate(confidences.begin(), confidences.end(), 0.0f) / confidences.size();
    }

    return {result, confidence};
}

TensorsTable PaddleOCRCtcConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    TensorsTable tensors_table;

    try {
        const size_t batch_size = getModelInputImageInfo().batch_size;
        tensors_table.resize(batch_size);

        for (const auto &blob_iter : output_blobs) {
            OutputBlob::Ptr blob = blob_iter.second;
            if (!blob) {
                throw std::invalid_argument("Output blob is empty");
            }

            const float *data = reinterpret_cast<const float *>(blob->GetData());
            if (!data) {
                throw std::invalid_argument("Output blob data is nullptr");
            }

            const std::string layer_name = blob_iter.first;

            // Output shape: [batch_size, seq_len, vocab_size]
            // Tensor vocab_size has two additional tokens: CTC Blank token and Padding token
            // hence its size is bigger than character vocabulary
            const auto &dims = blob->GetDims();
            const size_t vocab_size = (dims.size() == 3) ? dims[2] : 0;
            const size_t seq_len = (dims.size() >= 2) ? dims[1] : 0;
            if (vocab_size == 0 || seq_len == 0 || vocab_size != vocabulary.size() + 2)
                throw std::invalid_argument("Unexpected PaddleOCR output tensor dimensions");

            for (size_t batch_elem_index = 0; batch_elem_index < batch_size; ++batch_elem_index) {
                GVA::Tensor classification_result = createTensor();

                if (!raw_tensor_copying->enabled(RawTensorCopyingToggle::id))
                    CopyOutputBlobToGstStructure(blob, classification_result.gst_structure(),
                                                 BlobToMetaConverter::getModelName().c_str(), layer_name.c_str(),
                                                 batch_size, batch_elem_index);

                const float *item_data = data + batch_elem_index * seq_len * vocab_size;
                auto [decoded_text, confidence] = ctcDecode(item_data, seq_len, vocab_size);

                if (decoded_text.size() > seq_minlen) {
                    classification_result.set_string("label", decoded_text);
                    classification_result.set_double("confidence", confidence);
                } else {
                    classification_result.set_string("label", "");
                    classification_result.set_double("confidence", 0.0);
                }

                gst_structure_set(classification_result.gst_structure(), "tensor_id", G_TYPE_INT,
                                  safe_convert<int>(batch_elem_index), "type", G_TYPE_STRING, "classification_result",
                                  NULL);
                std::vector<GstStructure *> tensors{classification_result.gst_structure()};
                tensors_table[batch_elem_index].push_back(tensors);
            }
        }
    } catch (const std::exception &e) {
        GVA_ERROR("An error occurred in PaddleOCR CTC converter: %s", e.what());
    }

    return tensors_table;
}
