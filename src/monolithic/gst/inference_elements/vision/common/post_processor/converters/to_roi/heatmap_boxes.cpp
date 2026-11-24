/*******************************************************************************
 * Copyright (C) 2023-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "heatmap_boxes.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <gst/gst.h>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace post_processing;

void HeatMapBoxesConverter::parseOutputBlob(const float *data, const std::vector<size_t> &blob_dims,
                                            std::vector<DetectedObject> &objects) const {
    if (!data)
        throw std::invalid_argument("Output blob data is nullptr");

    if (blob_dims.size() < 4)
        throw std::invalid_argument("Invalid output blob dimensions (expecting 4): " + blob_dims.size());

    // NCHW format, extracting NHW data from first channel
    size_t height = blob_dims[2];
    size_t width = blob_dims[3];
    cv::Mat src = cv::Mat(cv::Size(width, height), CV_32F, const_cast<float *>(data));
    std::vector<cv::Mat> src_channels;
    cv::split(src, src_channels);

    // Binarize image to bitmap
    cv::Mat bitmap;
    cv::threshold(src_channels[0], bitmap, binarize_threshold, 255, cv::THRESH_BINARY);

    // Find contours - needs image to be CV_8U
    bitmap.convertTo(bitmap, CV_8U);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    // Form boxes from each contour
    for (auto &contour : contours) {
        // Calculate box dimensions from contour
        cv::Rect2d box = findBoxDimensions(contour);
        if (std::min(box.width, box.height) < minimum_side) {
            continue;
        }

        // Calculate box scores
        double confidence = boxScoreFast(src_channels[0], contour);
        if (confidence < confidence_threshold) {
            continue;
        }

        // Normalize box coordinates and dimensions
        size_t input_width = getModelInputImageInfo().width;
        size_t input_height = getModelInputImageInfo().height;
        double bbox_x = box.x / input_width;
        double bbox_y = box.y / input_height;
        double bbox_w = box.width / input_width;
        double bbox_h = box.height / input_height;

        // Object detection of single type i.e text, has no multiple labels
        DetectedObject bbox(bbox_x, bbox_y, bbox_w, bbox_h, 0, confidence, 0, getLabelByLabelId(0));
        objects.push_back(bbox);
    }
}

TensorsTable HeatMapBoxesConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        size_t batch_size = model_input_image_info.batch_size;
        DetectedObjectsTable objects_table(batch_size);
        for (size_t batch_number = 0; batch_number < batch_size; ++batch_number) {
            auto &objects = objects_table[batch_number];
            for (const auto &blob_iter : output_blobs) {
                const InferenceBackend::OutputBlob::Ptr &blob = blob_iter.second;
                if (!blob)
                    throw std::invalid_argument("Output blob is nullptr.");
                size_t unbatched_size = blob->GetSize() / batch_size;

                if (!blob->GetData())
                    throw std::runtime_error("Output blob data is nullptr.");

                if (blob->GetPrecision() != InferenceBackend::Blob::Precision::FP32)
                    throw std::runtime_error("Unsupported label precision.");

                parseOutputBlob(reinterpret_cast<const float *>(blob->GetData()) + unbatched_size * batch_number,
                                blob->GetDims(), objects);
            }
        }
        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do heatmap post-processing."));
    }
    return TensorsTable{};
}

cv::Rect2d HeatMapBoxesConverter::findBoxDimensions(std::vector<cv::Point> &contour) const {
    cv::RotatedRect rot_rect = cv::minAreaRect(contour);
    return rot_rect.boundingRect();
}

double HeatMapBoxesConverter::boxScoreFast(const cv::Mat src_channel_0, std::vector<cv::Point> &contour) const {
    // Find the enclosing box coordinates for a contour
    // Round up each coordinate value with floor/ceiling as applicable, cast to integer and clip value between 0 and
    // width/height
    int width = src_channel_0.size().width;
    int height = src_channel_0.size().height;
    auto x_min_max = std::minmax_element(contour.begin(), contour.end(),
                                         [](const cv::Point &a, const cv::Point &b) { return a.x <= b.x; });
    int xmin = std::clamp(static_cast<int>(floor(x_min_max.first->x)), 0, width - 1);
    int xmax = std::clamp(static_cast<int>(ceil(x_min_max.second->x)), 0, width - 1);
    auto y_min_max = std::minmax_element(contour.begin(), contour.end(),
                                         [](const cv::Point &a, const cv::Point &b) { return a.y <= b.y; });
    int ymin = std::clamp(static_cast<int>(floor(y_min_max.first->y)), 0, height - 1);
    int ymax = std::clamp(static_cast<int>(ceil(y_min_max.second->y)), 0, height - 1);

    // Create a mask corresponding to the box location
    int rows = ymax - ymin + 1;
    int cols = xmax - xmin + 1;
    cv::Mat mask = cv::Mat::zeros(rows, cols, CV_8U);
    size_t num_points = contour.size();
    for (size_t i = 0; i < num_points; i++) {
        contour[i].x = contour[i].x - xmin;
        contour[i].y = contour[i].y - ymin;
    }
    cv::fillPoly(mask, contour, 1);

    // Use mask and bitmap of the box in the original image to calculate score
    auto sub_bitmap = src_channel_0(cv::Rect(xmin, ymin, cols, rows));
    double conf = mean(sub_bitmap, mask)[0];
    return conf;
}