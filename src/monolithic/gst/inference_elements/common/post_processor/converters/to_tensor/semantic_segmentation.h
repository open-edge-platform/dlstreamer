/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_tensor_converter.h"
#include "inference_backend/logger.h"

#include <string>

namespace post_processing {

class SemanticSegmentationConverter : public BlobToTensorConverter {
  private:
    const std::string format;

  public:
    SemanticSegmentationConverter(BlobToMetaConverter::Initializer initializer)
        : BlobToTensorConverter(std::move(initializer)), format("semantic_segmentation") {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "semantic_segmentation";
    }
};

} // namespace post_processing