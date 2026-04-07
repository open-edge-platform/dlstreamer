/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_roi_converter.h"

#include "inference_backend/image_inference.h"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace post_processing {

class BoxesLabelsScoresConverter : public BlobToROIConverter {
  private:
    static constexpr size_t batched_model_dimentions_size = 3;
    static constexpr size_t bbox_size_coordinates_confidence = 5;
    static constexpr size_t bbox_size_coordinates = 4;
    static const std::string boxes_layer_name;

  protected:
    void parseOutputBlob(const float *boxes_data, const std::vector<size_t> &blob_dims,
                         const InferenceBackend::OutputBlob::Ptr &labels_scores_blob,
                         std::vector<DetectedObject> &objects, const ModelImageInputInfo &model_input_image_info,
                         double roi_scale) const;

    virtual InferenceBackend::OutputBlob::Ptr getLabelsScoresBlob(const OutputBlobs &) const = 0;
    virtual std::pair<size_t, float> getLabelIdConfidence(const InferenceBackend::OutputBlob::Ptr &, size_t,
                                                          float) const = 0;
    virtual std::tuple<float, float, float, float> getBboxCoordinates(const float *bbox_data, size_t width,
                                                                      size_t height) const;

  public:
    BoxesLabelsScoresConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold,
                               double iou_threshold)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, true, iou_threshold) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static bool isValidModelBoxesOutput(const std::map<std::string, std::vector<size_t>> &model_outputs_info);
    static bool isValidModelAdditionalOutput(const std::map<std::string, std::vector<size_t>> &model_outputs_info,
                                             const std::string &additional_layer_name);
};
} // namespace post_processing
