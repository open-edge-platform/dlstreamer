/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_tensor_converter.h"

#include "post_processor/zeroshot_embeddings.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace post_processing {

// Zero-shot (open-vocabulary) image classification with CLIP-family models.
//
// gvaclassify runs the CLIP image encoder (vision tower + visual projection) as its model; this
// converter turns the resulting projected image embedding into ranked class scores by cosine
// similarity against precomputed text-label embeddings. The label set lives entirely outside the
// model, so classes can be changed by swapping the embeddings file with no retraining. The
// similarity, temperature scaling and top-k all run on the host CPU, so the model graph stays
// exactly the vision tower (important for static-shape NPU execution).
//
// The converter is pure: the class bank arrives ready-to-use as a ZeroShotEmbeddings (already
// L2-normalized, with logit_scale and unknown_threshold resolved) via the Initializer. All file and
// safetensors knowledge lives in loadEmbeddingsFromFile() in the post-processor setup layer.
// Normalizing the incoming image embedding stays here, since that is per-frame data.
//
// Selection is driven by the model: an image encoder exported for zero-shot carries
// model_type=clip_zeroshot in its model_info rt_info, which resolves to this converter. The
// gvaclassify zeroshot-embeddings-file property supplies the class bank; it does not select the
// converter. Image preprocessing (CLIP mean/std, resize, color format) also comes from model_info,
// so no DL Streamer model-proc file is required.
//
// Not to be confused with clip_token, which emits the unprojected vision-tower output for
// image-to-image comparisons performed outside the pipeline.
class ClipZeroShotConverter : public BlobToTensorConverter {
  public:
    explicit ClipZeroShotConverter(BlobToMetaConverter::Initializer initializer);

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "clip_zeroshot";
    }

  private:
    ZeroShotEmbeddings embeddings_;
    uint32_t topk_ = 1;
    float logit_scale_ = 1.0f; // CLIP temperature applied before softmax

    // Raw cosine similarity of the (normalized) image embedding against every class prototype.
    std::vector<float> computeSimilarities(const float *image_embedding, std::size_t image_embedding_size) const;
    // softmax(logit_scale_ * similarities).
    std::vector<float> computeProbabilities(const std::vector<float> &similarities) const;
};

} // namespace post_processing
