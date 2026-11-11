/*******************************************************************************
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_tensor_converter.h"

#include "inference_backend/image_inference.h"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace post_processing {

class CLIPTokenConverter : public BlobToTensorConverter {
  public:
    CLIPTokenConverter(BlobToMetaConverter::Initializer initializer) : BlobToTensorConverter(std::move(initializer)) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "clip_token";
    }
};
} // namespace post_processing
