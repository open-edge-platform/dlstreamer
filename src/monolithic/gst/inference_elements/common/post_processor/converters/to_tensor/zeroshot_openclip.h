/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_tensor_converter.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace post_processing {

// Zero-shot image classification with OpenCLIP-family models.
//
// gvaclassify runs the CLIP image encoder (vision tower + visual projection) as its model; this
// converter turns the resulting image embedding into ranked class scores by cosine similarity
// against precomputed text-label embeddings loaded from a local .safetensors file. The label set
// lives entirely outside the model, so classes can be changed by swapping the embeddings file with
// no retraining. The similarity, temperature scaling and top-k all run on the host CPU, so the
// model graph stays exactly the vision tower (important for static-shape NPU execution).
//
// The embeddings file is a single 2-D F32/F16 tensor of shape [num_classes, embedding_dim] with rows
// aligned to the configured labels. Its optional metadata may carry:
//   - "logit_scale" (CLIP temperature): applied before the softmax so confidences are calibrated.
//   - "unknown_threshold" (double): if the top-1 cosine similarity is below this value the result is
//     labelled "unknown" (label_id -1). Omitted/negative disables the check.
//
// Selected either implicitly (gvaclassify zeroshot-embeddings-file=...) or via model-proc
// ("converter": "zeroshot_openclip"). Image preprocessing (CLIP mean/std, resize, color format) comes
// from the model's own model_info section in model.xml, so no DL Streamer model-proc file is required.
class ZeroShotOpenCLIPConverter : public BlobToTensorConverter {
  public:
    explicit ZeroShotOpenCLIPConverter(BlobToMetaConverter::Initializer initializer);

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "zeroshot_openclip";
    }

  private:
    std::vector<float> embeddings_; // row-major [num_classes_ x embedding_dim_], L2-normalized rows
    std::size_t num_classes_ = 0;
    std::size_t embedding_dim_ = 0;
    uint32_t topk_ = 1;
    float logit_scale_ = 0.0f;         // CLIP temperature applied before softmax; 0 => unset
    double unknown_threshold_ = -1.0;  // < 0 disables; else min top-1 cosine similarity to accept

    void loadEmbeddings(const std::string &embeddings_path);
    // Raw cosine similarity of the (normalized) image embedding against every class prototype.
    std::vector<float> computeSimilarities(const float *image_embedding, std::size_t image_embedding_size) const;
    // softmax(logit_scale_ * similarities).
    std::vector<float> computeProbabilities(const std::vector<float> &similarities) const;
};

} // namespace post_processing
