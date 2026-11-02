/*******************************************************************************
 * Copyright (C) 2025 Intel Corporation
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

class CustomToTensorConverter : public BlobToTensorConverter {
  private:
    using ConvertFunc = void (*)(GstTensorMeta *, const GstStructure *, const GstStructure *,
                                 GstAnalyticsRelationMeta *);

    const std::string custom_postproc_lib;

  public:
    CustomToTensorConverter(BlobToMetaConverter::Initializer initializer, const std::string &custom_postproc_lib)
        : BlobToTensorConverter(std::move(initializer)), custom_postproc_lib(custom_postproc_lib) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "custom_to_tensor";
    }
};
} // namespace post_processing
