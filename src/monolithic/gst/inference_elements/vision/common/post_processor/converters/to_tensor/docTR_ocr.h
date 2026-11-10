/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "blob_to_tensor_converter.h"
#include <deque>
#include <string>
#include <vector>

#define DEF_MODEL_SEQ_LEN 32
#define DEF_MODEL_CHARSET_LEN 124
#define DEF_USED_CHARSET "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"; // Default character set
#define DEF_HISTORY_LEN 5
#define DEF_N_OCCUR 3
#define DEF_MAXLEN 8
#define DEF_MINLEN 4

namespace post_processing {

class docTROCRConverter : public BlobToTensorConverter {
  public:
    docTROCRConverter(BlobToMetaConverter::Initializer initializer);
    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "docTR_ocr";
    }

  private:
    std::string used_character_set = DEF_USED_CHARSET;
    size_t sequence_length = DEF_MODEL_SEQ_LEN;
    size_t num_classes = DEF_MODEL_CHARSET_LEN;
    size_t history_len = DEF_HISTORY_LEN;
    size_t n_occurrences = DEF_N_OCCUR;
    size_t seq_minlen = DEF_MINLEN;
    size_t seq_maxlen = DEF_MAXLEN;
    std::deque<std::string> text_buffer; // Deque to store texts with a maximum size of N

    std::string decodeSequence(const float *probabilities, size_t size) const;
    std::vector<float> softmax(const float *data, size_t num_classes) const;
    std::string getMostCommonText() const;
    void addText(const std::string &text);
};
} // namespace post_processing