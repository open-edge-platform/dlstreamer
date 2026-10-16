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
RT-DETR / D-FINE tensor output layout:
    logits: [B, N, C] or [N, C]
    boxes:  [B, N, 4] or [N, 4] in normalized cxcywh
    With use_focal_loss, scores are per-class sigmoids (as in transformers post_process_object_detection);
    otherwise softmax, where C may include an extra "no-object" class at the end.
*/
class RTDETRConverter : public BlobToROIConverter {
  protected:
    bool use_focal_loss = false;

    void parseOutputBlobs(const float *logits_data, const std::vector<size_t> &logits_dims, const float *boxes_data,
                          const std::vector<size_t> &boxes_dims, std::vector<DetectedObject> &objects) const;

  public:
    RTDETRConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold);

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "rtdetr";
    }
};

} // namespace post_processing