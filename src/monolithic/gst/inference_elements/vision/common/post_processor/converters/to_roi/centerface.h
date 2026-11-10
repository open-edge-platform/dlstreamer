/*******************************************************************************
 * Copyright (C) 2024-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_roi_converter.h"
#include "inference_backend/image_inference.h"

#include <gst/gst.h>
#include <opencv2/opencv.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace post_processing {

const std::string HEATMAP_KEY = "heatmap";
const std::string SCALE_KEY = "scale";
const std::string OFFSET_KEY = "offset";
const std::string LANDMARKS_KEY = "landmarks";

const int NUMBER_OF_LANDMARK_POINTS = 5;

class CenterfaceConverter : public BlobToROIConverter {
  public:
    CenterfaceConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, true, iou_threshold) {
    }

    void decode(const float *heatmap, int heatmap_height, int heatmap_width, const float *scale, const float *offset,
                const float *landmarks, std::vector<DetectedObject> &faces, float scoreThresh, size_t input_width,
                size_t input_height) const;
    const float *parseOutputBlob(const OutputBlobs &output_blobs, const std::string &key, size_t batch_size,
                                 size_t batch_number) const;
    void addLandmarksTensor(DetectedObject &detected_object, const float *landmarks, int num_of_landmarks) const;
    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "centerface";
    }
};
} // namespace post_processing
