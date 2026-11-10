/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
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

class LabelConverter : public BlobToTensorConverter {
  public:
    enum class Method { Max, SoftMax, Compound, Multi, SoftMaxMulti, Index, Default = Max };

    LabelConverter(BlobToMetaConverter::Initializer initializer);

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "label";
    }
    static std::string getDeprecatedName() {
        return "tensor_to_label";
    }

  private:
    Method _method = Method::Max;
    double _confidence_threshold = 0.5;
    template <typename T>
    void ExecuteMethod(const T *data, const std::string &layer_name, InferenceBackend::OutputBlob::Ptr blob,
                       TensorsTable &tensors_table) const;
};
} // namespace post_processing
