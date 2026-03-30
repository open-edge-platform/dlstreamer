/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "keypoints_hrnet.h"

#include "blob_to_tensor_converter.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "tensor.h"

#include <dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h>

#include <gst/gst.h>

#include <opencv2/imgproc.hpp>

#include <exception>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace post_processing;

static const GstAnalyticsKeypointDescriptor &hrnet_coco17_descriptor = gst_keypoint_descriptor_hrnet_coco17;

TensorsTable KeypointsHRnetConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    TensorsTable tensors_table;
    try {
        const size_t batch_size = getModelInputImageInfo().batch_size;
        if (batch_size != 1)
            throw std::runtime_error("Converter does not support batch_size != 1.");

        tensors_table.resize(batch_size);
        auto &tensors = tensors_table[0];

        for (const auto &blob_iter : output_blobs) {
            const auto &blob = blob_iter.second;
            if (not blob) {
                throw std::invalid_argument("Output blob is empty");
            }

            const auto &dims = blob->GetDims();
            size_t points_num = dims[1];
            size_t height = dims[2];
            size_t width = dims[3];
            auto map_size = height * width;

            GstStructure *tensor_data = createTensor(GVA_PRECISION_FP32, {points_num, 2});

            std::vector<cv::Point2f> real_keypoints;
            real_keypoints.reserve(points_num);

            const float *batch_heatmaps = reinterpret_cast<const float *>(blob->GetData());
            if (not batch_heatmaps)
                throw std::invalid_argument("Output blob data is nullptr");

            for (size_t i = 0; i < points_num; ++i) {
                cv::Mat heatmap_image(height, width, CV_32FC1, const_cast<float *>(batch_heatmaps + i * map_size));

                cv::Point min_loc, joint_loc;
                double min, joint;
                cv::minMaxLoc(heatmap_image, &min, &joint, &min_loc, &joint_loc);

                cv::Point2f real_point(static_cast<float>(joint_loc.x) / width,
                                       static_cast<float>(joint_loc.y) / height);

                real_keypoints.push_back(real_point);
            }

            // set coordinates relative output map
            copyKeypointsToGstStructure(tensor_data, real_keypoints);

            // set descriptor metadata: format, point names, skeleton connections
            GVA::Tensor tensor(tensor_data);
            tensor.set_format(hrnet_coco17_descriptor.semantic_tag);
            std::vector<std::string> names(hrnet_coco17_descriptor.point_names,
                                           hrnet_coco17_descriptor.point_names + hrnet_coco17_descriptor.point_count);
            std::vector<uint32_t> connections(
                hrnet_coco17_descriptor.skeleton_connections,
                hrnet_coco17_descriptor.skeleton_connections + hrnet_coco17_descriptor.skeleton_connection_count * 2);
            tensor.set_vector<std::string>("point_names", names);
            tensor.set_vector<uint32_t>("point_connections", connections);

            std::vector<GstStructure *> result{tensor_data};
            tensors.push_back(result);
        }

    } catch (const std::exception &e) {
        GVA_ERROR("An error occurred at keypoints HRnet converter: %s", e.what());
    }

    return tensors_table;
}
