/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "boxes_labels_scores_base.h"

#include "inference_backend/image_inference.h"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace post_processing {

class BoxesLabelsConverter : public BoxesLabelsScoresConverter {
  private:
    static const std::string labels_layer_name;

  protected:
    InferenceBackend::OutputBlob::Ptr getLabelsScoresBlob(const OutputBlobs &) const final;
    std::pair<size_t, float> getLabelIdConfidence(const InferenceBackend::OutputBlob::Ptr &, size_t, float) const final;

  public:
    BoxesLabelsConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold,
                         double iou_threshold)
        : BoxesLabelsScoresConverter(std::move(initializer), confidence_threshold, iou_threshold) {
    }

    static bool isValidModelOutputs(const std::map<std::string, std::vector<size_t>> &model_outputs_info);

    static std::string getName() {
        return "boxes_labels";
    }
    static std::string getDeprecatedName() {
        return "tensor_to_bbox_atss";
    }
};
} // namespace post_processing
