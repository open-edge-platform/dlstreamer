/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
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

class DepthConverter : public BlobToTensorConverter {
  public:
    DepthConverter(BlobToMetaConverter::Initializer initializer);

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "depth";
    }
};
} // namespace post_processing