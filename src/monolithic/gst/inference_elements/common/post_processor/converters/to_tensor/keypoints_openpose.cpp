/*******************************************************************************
 * Copyright (C) 2020-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "keypoints_openpose.h"

#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"
#include "tensor.h"
#include "video_frame.h"

#include <dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>

#include <vector>

using namespace post_processing;

static const GstAnalyticsKeypointDescriptor &openpose18_descriptor = gst_keypoint_descriptor_openpose18;

TensorsTable KeypointsOpenPoseConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    TensorsTable tensors_table;
    try {
        const size_t batch_size = getModelInputImageInfo().batch_size;

        // TODO: get layer names from model_proc
        const auto &heat_map_blob = output_blobs.at("Mconv7_stage2_L2");
        const auto &heat_map_dims = heat_map_blob->GetDims();
        const size_t heat_map_offset = heat_map_dims[2] * heat_map_dims[3];
        const size_t n_heat_maps = heat_map_dims[1];

        // TODO: get layer names from model_proc
        const auto &pafs_blob = output_blobs.at("Mconv7_stage2_L1");
        const auto &pafs_dims = pafs_blob->GetDims();
        const size_t paf_offset = heat_map_dims[2] * heat_map_dims[3];
        const size_t n_pafs = pafs_dims[1];

        const size_t feature_map_width = heat_map_dims[3];
        const size_t feature_map_height = heat_map_dims[2];

        if (batch_size != heat_map_dims[0] || batch_size != pafs_dims[0])
            throw std::runtime_error("Batch size of heat-map and pafs-map outputs should be equal.");

        const float *heat_maps_data = reinterpret_cast<const float *>(heat_map_blob->GetData());
        const float *pafs_data = reinterpret_cast<const float *>(pafs_blob->GetData());
        if (!heat_maps_data || !pafs_data)
            throw std::runtime_error("Heat-map or pafs-map are empty.");

        for (size_t batch_index = 0; batch_index < batch_size; ++batch_index) {
            heat_maps_data += heat_map_offset * n_heat_maps * batch_index;
            pafs_data += paf_offset * n_pafs * batch_index;

            HumanPoseExtractor::HumanPoses poses =
                extractor.postprocess(heat_maps_data, heat_map_offset, n_heat_maps, pafs_data, paf_offset, n_pafs,
                                      feature_map_width, feature_map_height);

            extractor.correctCoordinates(poses,
                                         {safe_convert<int>(feature_map_width), safe_convert<int>(feature_map_height)});

            std::vector<std::vector<GstStructure *>> tensor_poses;
            for (const auto &pose : poses) {
                GstStructure *tensor_data = createTensor(GVA_PRECISION_FP32, {pose.keypoints.size(), 2});

                // set coordinates relative output map
                copyKeypointsToGstStructure(tensor_data, pose.keypoints);

                // set descriptor metadata: format, point names, skeleton connections
                GVA::Tensor tensor(tensor_data);
                tensor.set_format(openpose18_descriptor.semantic_tag);
                std::vector<std::string> names(openpose18_descriptor.point_names,
                                               openpose18_descriptor.point_names + openpose18_descriptor.point_count);
                std::vector<uint32_t> connections(
                    openpose18_descriptor.skeleton_connections,
                    openpose18_descriptor.skeleton_connections + openpose18_descriptor.skeleton_connection_count * 2);
                tensor.set_vector<std::string>("point_names", names);
                tensor.set_vector<uint32_t>("point_connections", connections);

                std::vector<GstStructure *> result{tensor_data};
                tensor_poses.push_back(result);
            }

            tensors_table.push_back(tensor_poses);
        }

    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do \"KeypointsOpenPoseConverter\" post-processing"));
    }

    return tensors_table;
}
