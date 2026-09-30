/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace post_processing {

// Precomputed zero-shot class bank, ready for use by the clip_zeroshot converter.
//
// Produced by loadEmbeddingsFromFile() in the post-processor setup layer, so the converter itself
// stays free of file and format knowledge (same convention as loadLabelsFromFile). Rows are the
// projected text embeddings of the class prompts, aligned with the configured labels.
struct ZeroShotEmbeddings {
    // Row-major [num_classes * embedding_dim]; every row is L2-normalized.
    std::vector<float> class_embeddings;
    std::size_t num_classes = 0;
    std::size_t embedding_dim = 0;
    // CLIP temperature applied before the softmax so confidences are calibrated; <= 0 means unset.
    float logit_scale = 0.0f;
    // Minimum top-1 cosine similarity to accept a class; negative disables the check.
    double unknown_threshold = -1.0;

    bool empty() const {
        return num_classes == 0;
    }
};

// Reads a zero-shot class bank from a .safetensors file: parses the 2-D embeddings matrix,
// L2-normalizes its rows, and picks up the optional "logit_scale" / "unknown_threshold" entries
// from the file's __metadata__. Throws std::invalid_argument / std::runtime_error on a missing,
// unreadable or malformed file.
ZeroShotEmbeddings loadEmbeddingsFromFile(const std::string &embeddings_file);

} // namespace post_processing
