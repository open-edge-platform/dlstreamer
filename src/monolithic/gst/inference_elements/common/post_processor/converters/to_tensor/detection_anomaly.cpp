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
        double image_threshold_norm = normalize(image_threshold);
        double pixel_threshold_norm = normalize(pixel_threshold);
        cv::Mat anomaly_map;
        cv::Mat seg_mask;
        std::string pred_label = "";

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

            anomaly_map = cv::Mat((int)img_height, (int)img_width, CV_32FC1, const_cast<float *>(data));
            anomaly_map = anomaly_map / normalization_scale;
            anomaly_map = cv::min(cv::max(anomaly_map, 0.f), 1.f); // [0, 1] normalization
            // find the the highest anomaly score in the anomaly map
            cv::minMaxLoc(anomaly_map, NULL, &pred_score);

            // create segmentation mask from anomaly map using pixel-level threshold
            cv::threshold(anomaly_map, seg_mask, pixel_threshold_norm, 1.0f, cv::THRESH_BINARY);

            const auto &labels = getLabels();
            if (labels.size() != DEF_TOTAL_LABELS_COUNT)
                throw std::runtime_error("Anomaly-detection converter: Expected 2 labels, got: " +
                                         std::to_string(labels.size()));

            pred_label = labels[pred_score > image_threshold_norm ? 1 : 0];

            logParamsStats(pred_label, pred_score, image_threshold_norm);

            const std::string layer_name = blob_iter.first;
            for (size_t frame_index = 0; frame_index < batch_size; ++frame_index) {
                GVA::Tensor classification_result = createTensor();

                classification_result.set_string("label", pred_label);
                classification_result.set_double("confidence", pred_score);

                gst_structure_set(classification_result.gst_structure(), "tensor_id", G_TYPE_INT,
                                  safe_convert<int>(frame_index), "type", G_TYPE_STRING, "classification_result",
                                  "precision", G_TYPE_INT, static_cast<int>(blob->GetPrecision()), NULL);

                // Add segmentation mask data if anomaly detected
                if (pred_label == "Anomaly") {

                    classification_result.set_format("segmentation_mask");
                    classification_result.set_dims(
                        {safe_convert<uint32_t>(seg_mask.cols), safe_convert<uint32_t>(seg_mask.rows)});
                    classification_result.set_precision(GVA::Tensor::Precision::FP32);
                    classification_result.set_data(reinterpret_cast<const void *>(seg_mask.data),
                                                   seg_mask.rows * seg_mask.cols * sizeof(float));
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

void DetectionAnomalyConverter::logParamsStats(const std::string &pred_label, const double &pred_score,
                                               const double &image_threshold_norm) {
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

    GVA_INFO("pred_label: %s, pred_score: %f, image_threshold: %f, "
             "image_threshold_norm: %f, normalization_scale: %f, #normal: %u, #anomaly: %u",
             pred_label.c_str(), pred_score, image_threshold, image_threshold_norm, normalization_scale, lbl_normal_cnt,
             lbl_anomaly_cnt);
}
