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

class BoxesScoresConverter : public BoxesLabelsScoresConverter {
  private:
    bool do_cls_softmax = false;
    static const std::string scores_layer_name;

  protected:
    InferenceBackend::OutputBlob::Ptr getLabelsScoresBlob(const OutputBlobs &) const final;
    std::pair<size_t, float> getLabelIdConfidence(const InferenceBackend::OutputBlob::Ptr &, size_t, float) const final;
    std::tuple<float, float, float, float> getBboxCoordinates(const float *bbox_data, size_t width,
                                                              size_t height) const override final;

  public:
    BoxesScoresConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold,
                         double iou_threshold)
        : BoxesLabelsScoresConverter(std::move(initializer), confidence_threshold, iou_threshold) {
        const GstStructure *s = getModelProcOutputInfo().get();
        gboolean do_cls_sftm = FALSE;
        if (gst_structure_has_field(s, "do_cls_softmax")) {
            gst_structure_get_boolean(s, "do_cls_softmax", &do_cls_sftm);
        }

        do_cls_softmax = do_cls_sftm;
    }

    static bool isValidModelOutputs(const std::map<std::string, std::vector<size_t>> &model_outputs_info);

    static std::string getName() {
        return "boxes_scores";
    }
};
} // namespace post_processing
