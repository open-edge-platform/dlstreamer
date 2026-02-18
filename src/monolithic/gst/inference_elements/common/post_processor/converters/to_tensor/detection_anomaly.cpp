/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "detection_anomaly.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/logger.h"

#include <algorithm>
#include <exception>
#include <gst/gst.h>
#include <map>
#include <memory>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

using namespace post_processing;
using namespace InferenceBackend;

TensorsTable DetectionAnomalyConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    TensorsTable tensors_table;

    try {
        const size_t batch_size = getModelInputImageInfo().batch_size;
        tensors_table.resize(batch_size);

        double pred_score = 0.0;
        cv::Mat anomaly_map;
        cv::Mat anomaly_map_raw;
        cv::Mat pred_mask;
        std::string pred_label = "";
        bool publish_pred_mask = false;

        for (const auto &blob_iter : output_blobs) {
            OutputBlob::Ptr blob = blob_iter.second;
            if (!blob) {
                throw std::invalid_argument("Output blob is empty");
            }

            const float *data = reinterpret_cast<const float *>(blob->GetData());
            if (!data) {
                throw std::invalid_argument("Output blob data is nullptr");
            }

            const auto &dims = blob->GetDims();
            if (dims.size() != DEF_ANOMALY_TENSOR_LAYOUT_SIZE) {
                throw std::runtime_error(
                    "Anomaly-detection converter supports only 4-dimensional output tensors got: " +
                    std::to_string(dims.size()));
            }
            if (dims[DEF_ANOMALY_TENSOR_LAYOUT_OFFSET_CH] != 1) {
                throw std::runtime_error("Anomaly-detection converter output tensors must have second dimension equal "
                                         "to 1. It is one-channel, binary map."
                                         "got: " +
                                         std::to_string(dims[DEF_ANOMALY_TENSOR_LAYOUT_OFFSET_CH]));
            }
            const size_t img_height = dims[DEF_ANOMALY_TENSOR_LAYOUT_OFFSET_H];
            const size_t img_width = dims[DEF_ANOMALY_TENSOR_LAYOUT_OFFSET_W];

            anomaly_map_raw = cv::Mat((int)img_height, (int)img_width, CV_32FC1, const_cast<float *>(data));
            // Clamping the anomaly map to [0, 1] range
            anomaly_map_raw = cv::min(cv::max(anomaly_map_raw, 0.f), 1.f);
            // find the the highest anomaly score in the anomaly map
            cv::minMaxLoc(anomaly_map_raw, NULL, &pred_score);

            const auto &labels = getLabels();
            if (labels.size() != DEF_TOTAL_LABELS_COUNT)
                throw std::runtime_error("Anomaly-detection converter: Expected 2 labels, got: " +
                                         std::to_string(labels.size()));

            // classify using normalized threshold comparison
            pred_label = labels[pred_score > (image_threshold) ? 1 : 0];

            // normalize the score to [0, 1] range using the provided normalization scale and image threshold
            double pred_score_normalized = normalize(pred_score, image_threshold);
            // invert score for Normal predictions
            if (pred_label == "Normal") {
                pred_score_normalized = 1.0 - pred_score_normalized;
            }

            // Log the parameters and statistics
            logParamsStats(pred_label, pred_score_normalized, pred_score, image_threshold);

            const std::string layer_name = blob_iter.first;
            for (size_t frame_index = 0; frame_index < batch_size; ++frame_index) {
                GVA::Tensor classification_result = createTensor();

                classification_result.set_string("label", pred_label);
                classification_result.set_double("confidence", pred_score_normalized);

                gst_structure_set(classification_result.gst_structure(), "tensor_id", G_TYPE_INT,
                                  safe_convert<int>(frame_index), "type", G_TYPE_STRING, "classification_result",
                                  "precision", G_TYPE_INT, static_cast<int>(blob->GetPrecision()), NULL);

                // Add segmentation mask data if anomaly detected
                if (pred_label == "Anomaly" && publish_pred_mask) {

                    // Create binary mask by thresholding the anomaly map with pixel_threshold
                    // anomaly_map_raw is already normalized to [0, 1], so the pixel_threshold can be directly applied

                    pred_mask = anomaly_map_raw >= pixel_threshold;
                    pred_mask.convertTo(pred_mask, CV_8U); // Convert to uint8 (0 and 255)

                    classification_result.set_format("segmentation_mask");
                    classification_result.set_dims(
                        {safe_convert<uint32_t>(pred_mask.cols), safe_convert<uint32_t>(pred_mask.rows)});
                    classification_result.set_precision(GVA::Tensor::Precision::U8);
                    classification_result.set_data(reinterpret_cast<const void *>(pred_mask.data),
                                                   pred_mask.rows * pred_mask.cols * sizeof(uint8_t));
                }

                std::vector<GstStructure *> tensors{classification_result.gst_structure()};
                tensors_table[frame_index].push_back(tensors);
            }
        }
    } catch (const std::exception &e) {
        std::throw_with_nested(
            std::runtime_error("Anomaly-detection converter: Failed to convert output blobs to tensors table. "
                               "Error: " +
                               std::string(e.what())));
    }

    return tensors_table;
}

void DetectionAnomalyConverter::logParamsStats(const std::string &pred_label, const double &pred_score_normalized,
                                               const double &pred_score, const double &image_threshold) {
    if (pred_label == "Normal" || pred_label == "Anomaly") {
        if (pred_label == "Normal") {
            lbl_normal_cnt++;
        } else {
            lbl_anomaly_cnt++;
        }
    } else {
        throw std::runtime_error("Anomaly-detection converter: Not supported Label."
                                 "Expected 'Normal' or 'Anomaly', got: " +
                                 pred_label);
    }

    GVA_WARNING("pred_label: %s, pred_score_normalized: %f, pred_score: %f, image_threshold: %f, "
                "normalization_scale: %f, #normal: %u, #anomaly: %u",
                pred_label.c_str(), pred_score_normalized, pred_score, image_threshold, normalization_scale,
                lbl_normal_cnt, lbl_anomaly_cnt);
}
