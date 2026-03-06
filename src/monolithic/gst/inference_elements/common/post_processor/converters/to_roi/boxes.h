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

class BoxesConverter : public BoxesLabelsScoresConverter {
  protected:
    InferenceBackend::OutputBlob::Ptr getLabelsScoresBlob(const OutputBlobs &) const final {
        return nullptr;
    }
    std::pair<size_t, float> getLabelIdConfidence(const InferenceBackend::OutputBlob::Ptr &, size_t,
                                                  float conf) const final {
        return std::make_pair(0ul, conf);
    }

  public:
    BoxesConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : BoxesLabelsScoresConverter(std::move(initializer), confidence_threshold, iou_threshold) {
    }

    static bool isValidModelOutputs(const std::map<std::string, std::vector<size_t>> &model_outputs_info) {
        return BoxesLabelsScoresConverter::isValidModelBoxesOutput(model_outputs_info);
    }

    static std::string getName() {
        return "boxes";
    }
};
} // namespace post_processing
