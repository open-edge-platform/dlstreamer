/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
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

class DetectionOutputConverter : public BlobToROIConverter {
  protected:
    // FIXME: move roi_scale to coordinates restorer or attacher
    void parseOutputBlob(const InferenceBackend::OutputBlob::Ptr &blob, DetectedObjectsTable &objects,
                         double roi_scale) const;

  public:
    DetectionOutputConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, false, 0.0) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static const size_t model_object_size = 7; // SSD DetectionOutput format

    static bool isValidModelOutputs(const std::map<std::string, std::vector<size_t>> &model_outputs_info) {
        bool result = false;

        for (const auto &output : model_outputs_info) {
            const std::vector<size_t> &dims = output.second;
            if (dims.size() < BlobToROIConverter::min_dims_size)
                continue;

            if (dims[dims.size() - 1] == DetectionOutputConverter::model_object_size) {
                result = true;
                break;
            }
        }

        return result;
    }

    static std::string getName() {
        return "detection_output";
    }
    static std::string getDeprecatedName() {
        return "tensor_to_bbox_ssd";
    }
};
} // namespace post_processing
