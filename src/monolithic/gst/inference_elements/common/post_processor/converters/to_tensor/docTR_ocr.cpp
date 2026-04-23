/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "docTR_ocr.h"
#include "copy_blob_to_gststruct.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"
#include <algorithm>
#include <cmath>
#include <gst/gst.h>
#include <sstream>
#include <stdexcept>

#include <fstream>
#include <iostream>

using namespace post_processing;
using namespace InferenceBackend;

// Constructor to initialize the OCRConverter with the initializer.
docTROCRConverter::docTROCRConverter(BlobToMetaConverter::Initializer initializer)
    : BlobToTensorConverter(std::move(initializer)) {
}

TensorsTable docTROCRConverter::convert(const OutputBlobs &output_blobs) {
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

            for (size_t frame_index = 0; frame_index < batch_size; ++frame_index) {
                GVA::Tensor classification_result = createTensor();

                if (!skipRawTensors())
                    CopyOutputBlobToGstStructure(blob, classification_result.gst_structure(),
                                                 BlobToMetaConverter::getModelName().c_str(), layer_name.c_str(),
                                                 batch_size, frame_index);

                const auto item = get_data_by_batch_index(data, data_size, batch_size, frame_index);
                const float *item_data = item.first;

                // Construct the label string by finding the max index for each row
                std::string label_text;
                for (size_t seq_index = 0; seq_index < sequence_length; ++seq_index) {
                    const float *sequence_data = item_data + seq_index * num_classes;
                    std::vector<float> probabilities = softmax(sequence_data, num_classes);

                    // Find the index of the maximum probability
                    auto max_elem = std::max_element(probabilities.begin(), probabilities.end());
                    size_t index = std::distance(probabilities.begin(), max_elem);

                    // Extract character if index is within bounds
                    if (index < used_character_set.size()) {
                        if (label_text.size() < seq_maxlen)
                            label_text += used_character_set[index];
                    }
                }

                addText(label_text);
                std::string most_common_text = label_text;

                if (n_occurrences > 1)
                    most_common_text = getMostCommonText();

                // Set the label text as the label in the tensor
                if (label_text.size() > seq_minlen)
                    classification_result.set_string("label", most_common_text);
                else
                    classification_result.set_string("label", "");

                // Set metadata for the tensor in the GstStructure
                gst_structure_set(classification_result.gst_structure(), "tensor_id", G_TYPE_INT,
                                  safe_convert<int>(frame_index), "type", G_TYPE_STRING, "classification_result", NULL);
                std::vector<GstStructure *> tensors{classification_result.gst_structure()};
                tensors_table[frame_index].push_back(tensors);
            }
        }
    } catch (const std::exception &e) {
        GVA_ERROR("An error occurred in OCR converter: %s", e.what());
    }

    return tensors_table;
}

// Function to decode the sequence of probabilities into a text string using softmax.
std::string docTROCRConverter::decodeSequence(const float *probabilities, size_t size) const {
    std::string decoded_text;
    size_t num_classes = used_character_set.size();

    for (size_t i = 0; i < size / num_classes; ++i) {
        // Apply softmax to the slice of probabilities for the current position
        std::vector<float> softmax_probs = softmax(probabilities + i * num_classes, num_classes);
        // Find the index of the maximum probability after softmax
        auto max_elem = std::max_element(softmax_probs.begin(), softmax_probs.end());
        long unsigned int index = std::distance(softmax_probs.begin(), max_elem);

        if (index < used_character_set.size()) {
            decoded_text += used_character_set[index];
        }
    }

    return decoded_text;
}

// Function to compute softmax probabilities for a single sequence
std::vector<float> docTROCRConverter::softmax(const float *data, size_t num_classes) const {
    std::vector<float> softmax_probs(num_classes);
    float max_val = *std::max_element(data, data + num_classes);
    float sum_exp = 0.0f;

    for (size_t i = 0; i < num_classes; ++i) {
        softmax_probs[i] = std::exp(data[i] - max_val);
        sum_exp += softmax_probs[i];
    }

    for (size_t i = 0; i < num_classes; ++i) {
        softmax_probs[i] /= sum_exp;
    }

    return softmax_probs;
}

// Method to add text
void docTROCRConverter::addText(const std::string &text) {
    if (text_buffer.size() == history_len) {
        text_buffer.pop_front(); // Remove the oldest element if the deque is full
    }
    text_buffer.push_back(text);
}

// Method to get the most common text
std::string docTROCRConverter::getMostCommonText() const {
    if (text_buffer.empty()) {
        return "";
    }

    // Count occurrences of each text
    std::unordered_map<std::string, size_t> counter;
    for (const auto &text : text_buffer) {
        ++counter[text];
    }

    // Find the most common text
    std::string mostCommonText;
    size_t maxCount = 0;
    for (const auto &pair : counter) {
        if (pair.second > maxCount) {
            mostCommonText = pair.first;
            maxCount = pair.second;
        }
    }

    // Return the most common text only if it appears at least T times
    return maxCount >= n_occurrences ? mostCommonText : "";
}
