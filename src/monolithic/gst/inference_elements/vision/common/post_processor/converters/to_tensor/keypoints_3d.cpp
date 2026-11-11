/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "keypoints_3d.h"

#include "blob_to_tensor_converter.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"

#include <gst/gst.h>

#include <opencv2/imgproc.hpp>

#include <exception>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace post_processing;

TensorsTable Keypoints3DConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    TensorsTable tensors_table;
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        const size_t batch_size = model_input_image_info.batch_size;
        const size_t model_input_image_width = model_input_image_info.width;
        const size_t model_input_image_height = model_input_image_info.height;

        if (batch_size != 1)
            throw std::runtime_error("Converter does not support batch_size != 1.");

        tensors_table.resize(batch_size);
        auto &tensors = tensors_table[0];
        const size_t feature_scale_parameter = 4;

        for (const auto &blob_iter : output_blobs) {
            const auto &blob = blob_iter.second;
            if (not blob) {
                throw std::invalid_argument("Output blob is empty");
            }

            const auto &dims = blob->GetDims();
            size_t points_num = dims[1];
            size_t dimension = dims[2];

            if (dimension != 3)
                throw std::invalid_argument("Expected 3D model exit coordinates");

            GstStructure *tensor_data = createTensor(GVA_PRECISION_FP32, {points_num, dimension});

            std::vector<cv::Point3f> real_keypoints;
            real_keypoints.reserve(points_num);

            const auto *points_data = reinterpret_cast<const float *>(blob->GetData());
            if (not points_data)
                throw std::invalid_argument("Output blob data is nullptr");

            for (size_t i = 0; i < points_num; ++i) {
                const float *current_point = points_data + i * dimension;
                float x = current_point[0] * feature_scale_parameter;
                float y = current_point[1] * feature_scale_parameter;
                float z = current_point[2] * feature_scale_parameter;

                cv::Point3f parsed_point(x / model_input_image_width, y / model_input_image_height, z);
                real_keypoints.push_back(parsed_point);
            }

            // set coordinates relative output map
            copyKeypointsToGstStructure(tensor_data, real_keypoints);

            std::vector<GstStructure *> result{tensor_data};
            tensors.push_back(result);
        }

    } catch (const std::exception &e) {
        GVA_ERROR("An error occurred at keypoints 3D converter: %s", e.what());
    }

    return tensors_table;
}
