/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
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

class TextConverter : public BlobToTensorConverter {
    double scale = 1.0;
    int precision = 2;

  public:
    TextConverter(BlobToMetaConverter::Initializer initializer) : BlobToTensorConverter(std::move(initializer)) {
        GstStructure *s = getModelProcOutputInfo().get();

        gst_structure_get_double(s, "text_scale", &scale);
        gst_structure_get_int(s, "text_precision", &precision);
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "text";
    }
    static std::string getDeprecatedName() {
        return "tensor_to_text";
    }
};
} // namespace post_processing
