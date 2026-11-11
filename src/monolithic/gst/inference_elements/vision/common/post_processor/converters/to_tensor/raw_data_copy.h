/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
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

class RawDataCopyConverter : public BlobToTensorConverter {
  public:
    RawDataCopyConverter(BlobToMetaConverter::Initializer initializer) : BlobToTensorConverter(std::move(initializer)) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "raw_data_copy";
    }
};
} // namespace post_processing
