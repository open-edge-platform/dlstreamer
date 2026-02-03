/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_roi_converter.h"
#include "inference_backend/image_inference.h"

#include <gst/gst.h>

#include <string>
#include <vector>

namespace post_processing {

/*
RT-DETR tensor output layout:
    logits: [B, N, C] or [N, C]
    boxes:  [B, N, 4] or [N, 4] in normalized cxcywh
    where C may include an extra "no-object" class at the end.
*/
class RTDETRConverter : public BlobToROIConverter {
  protected:
    void parseOutputBlobs(const float *logits_data, const std::vector<size_t> &logits_dims, const float *boxes_data,
                          const std::vector<size_t> &boxes_dims, std::vector<DetectedObject> &objects) const;

  public:
    RTDETRConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, false, 0.0) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "rtdetr";
    }
};

} // namespace post_processing