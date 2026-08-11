/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_roi_converter.h"
#include "inference_backend/image_inference.h"
#include <string>
#include <vector>

namespace post_processing {

/*
RF-DETR tensor output layout:
    logits: [B, N, C] or [N, C] — raw logits, per-class sigmoid activation
    boxes:  [B, N, 4] or [N, 4] in normalized cxcywh
    No background class; all indices are valid object classes.
*/
class RFDETRConverter : public BlobToROIConverter {
  protected:
    void parseOutputBlobs(const float *logits_data, const std::vector<size_t> &logits_dims, const float *boxes_data,
                          const std::vector<size_t> &boxes_dims, std::vector<DetectedObject> &objects) const;

  public:
    RFDETRConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, false, 0.0) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "rfdetr";
    }
};

/*
RF-DETR instance segmentation: extends detection with per-query masks.
    masks:  [B, N, H, W] per-query mask logits
*/
class RFDETRSegConverter : public BlobToROIConverter {
  public:
    RFDETRSegConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, false, 0.0) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "rfdetr_seg";
    }
};

} // namespace post_processing