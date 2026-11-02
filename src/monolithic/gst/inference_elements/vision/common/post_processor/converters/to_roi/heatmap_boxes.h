/*******************************************************************************
 * Copyright (C) 2023-2025 Intel Corporation
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

#include "opencv2/imgproc.hpp"

namespace post_processing {

class HeatMapBoxesConverter : public BlobToROIConverter {
  protected:
    const double default_minimum_side = 5.0;
    const double default_binarize_threshold = 0.3;
    double minimum_side;
    double binarize_threshold;
    void parseOutputBlob(const float *data, const std::vector<size_t> &blob_dims,
                         std::vector<DetectedObject> &objects) const;
    double boxScoreFast(cv::Mat src, std::vector<cv::Point> &contour) const;
    cv::Rect2d findBoxDimensions(std::vector<cv::Point> &contour) const;
    double getMinimumSide(GstStructure *s, double default_value) {
        double minimum_side = default_value;
        if (gst_structure_has_field(s, "minimum_side")) {
            gst_structure_get_double(s, "minimum_side", &minimum_side);
        }
        return minimum_side;
    }

    double getBinarizeThreshold(GstStructure *s, double default_value) {
        double binarize_threshold = default_value;
        if (gst_structure_has_field(s, "binarize_threshold")) {
            gst_structure_get_double(s, "binarize_threshold", &binarize_threshold);
        }
        return binarize_threshold;
    }

  public:
    HeatMapBoxesConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, false, 0.0) {

        // Validate model_proc outputs and set parameters
        GstStructure *model_proc_output_info = getModelProcOutputInfo().get();
        const auto &model_input_image_info = getModelInputImageInfo();
        minimum_side = getMinimumSide(model_proc_output_info, default_minimum_side);
        binarize_threshold = getBinarizeThreshold(model_proc_output_info, default_binarize_threshold);
        const auto maximum_side = std::max(model_input_image_info.height, model_input_image_info.width);
        if (minimum_side < 0 || minimum_side > maximum_side) {
            throw std::invalid_argument("\"minimum_side\":" + std::to_string(minimum_side) +
                                        " in model-proc is invalid i.e < 0 OR > " + std::to_string(maximum_side));
        }
        if (binarize_threshold != std::clamp(binarize_threshold, 0.0, 255.0)) {
            throw std::invalid_argument("\"binarize_threshold\":" + std::to_string(binarize_threshold) +
                                        " in model-proc not within range [0,255]");
        }
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "heatmap_boxes";
    }
};
} // namespace post_processing
