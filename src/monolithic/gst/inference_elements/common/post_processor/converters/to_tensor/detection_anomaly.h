/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_tensor_converter.h"
#include <opencv2/imgproc.hpp>
#include <string>

#define DEF_ANOMALY_TENSOR_LAYOUT_OFFSET_BS 0 // batch size
#define DEF_ANOMALY_TENSOR_LAYOUT_OFFSET_CH 1 // single channel (anomaly map)
#define DEF_ANOMALY_TENSOR_LAYOUT_OFFSET_H 2  // image height
#define DEF_ANOMALY_TENSOR_LAYOUT_OFFSET_W 3  // image width
#define DEF_ANOMALY_TENSOR_LAYOUT_SIZE 4      // size of the tensor
#define DEF_TOTAL_LABELS_COUNT 2              // Normal and Anomaly

namespace post_processing {

class DetectionAnomalyConverter : public BlobToTensorConverter {
    double image_threshold; // Image threshold that is used for classifying an image as anomalous or normal
    double pixel_threshold; // Pixel threshold that is used for segmenting anomalous regions in the image
    double normalization_scale;
    std::string anomaly_detection_task;
    uint lbl_normal_cnt = 0;
    uint lbl_anomaly_cnt = 0;

  protected:
    // Helper function to normalize the thresholds
    double normalize(double &value, float threshold = 0.0) {
        double normalized = ((value - threshold) / normalization_scale) + 0.5f;
        return std::min(std::max(normalized, 0.), 1.);
    }

  public:
    DetectionAnomalyConverter(BlobToMetaConverter::Initializer initializer)
        : BlobToTensorConverter(std::move(initializer)) {
        validateAndLoadParameters();
    }
    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "AnomalyDetection";
    }

  private:
    void validateAndLoadParameters() {
        auto task_cstr = gst_structure_get_string(getModelProcOutputInfo().get(), "anomaly_task");
        if (std::string(task_cstr ? task_cstr : "") != DEFAULT_ANOMALY_DETECTION_TASK)
            throw std::runtime_error("<rt_info><model_info> parameter anomaly_task definition error: only "
                                     "'classification' is currently supported.");

        if (!gst_structure_get_double(getModelProcOutputInfo().get(), "normalization_scale", &normalization_scale))
            throw std::runtime_error("<rt_info><model_info> normalization_scale parameter undefined");

        if (!gst_structure_get_double(getModelProcOutputInfo().get(), "image_threshold", &image_threshold))
            throw std::runtime_error("<rt_info><model_info> image_threshold parameter undefined");

        if (!gst_structure_get_double(getModelProcOutputInfo().get(), "pixel_threshold", &pixel_threshold))
            throw std::runtime_error("<rt_info><model_info> pixel_threshold parameter undefined");
    }

    void logParamsStats(const std::string &pred_label, const double &pred_score_normalized, const double &pred_score,
                        const double &image_threshold);
};

} // namespace post_processing
