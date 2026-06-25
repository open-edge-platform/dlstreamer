/*******************************************************************************
 * Copyright (C) 2024-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_tensor_converter.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace post_processing {

/*
Masks output = [B, H, W] where:
    B - batch size
    H - mask height
    W - mask width
Output mask contains integer values, which represent an index of a predicted class for each image pixel
*/
class SemanticMaskConverter : public BlobToTensorConverter {
  private:
    const std::string format;

  public:
    SemanticMaskConverter(BlobToMetaConverter::Initializer initializer)
        : BlobToTensorConverter(std::move(initializer)), format(GVA::TENSOR_FORMAT_SEMANTIC_MASK) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "semantic_mask";
    }
};
} // namespace post_processing
