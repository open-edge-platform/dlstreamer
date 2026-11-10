/*******************************************************************************
 * Copyright (C) 2020-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "inference_backend/input_image_layer_descriptor.h"

#include <opencv2/core/core.hpp>

#include <string>
#include <vector>

class HumanPoseExtractor {
  public:
    const size_t keypoints_number;

    enum class ResizeDeviceType { CPU_OCV, GPU_OCV };
    struct HumanPose {
        HumanPose(const std::vector<cv::Point2f> &keypoints = std::vector<cv::Point2f>(), const float &score = 0)
            : keypoints(keypoints), score(score) {
        }

        std::vector<cv::Point2f> keypoints;
        float score;
    };

    using HumanPoses = std::vector<HumanPose>;

    HumanPoseExtractor(size_t, ResizeDeviceType maps_resize_device_type = ResizeDeviceType::CPU_OCV);

    HumanPoseExtractor() = delete;
    HumanPoseExtractor(const HumanPoseExtractor &) = default;
    ~HumanPoseExtractor() = default;

    HumanPoses postprocess(const float *heat_maps_data, const int heat_map_offset, const int n_heat_maps,
                           const float *pafs_data, const int paf_offset, const int n_pafs, const int feature_map_width,
                           const int feature_map_height) const;

    void correctCoordinates(HumanPoses &poses, cv::Size output_feature_map_size) const;

  private:
    HumanPoses extractPoses(const std::vector<cv::Mat> &heat_maps, const std::vector<cv::Mat> &pafs) const;
    void resizeFeatureMaps(std::vector<cv::Mat> &feature_maps) const;

    const int min_joints_number;
    const int stride;
    const cv::Vec3f mean_pixel;
    const float min_peaks_distance;
    const float mid_points_score_threshold;
    const float found_mid_points_ratio_threshold;
    const float min_subset_score;
    const int upsample_ratio;

    cv::Vec4i pad;
    cv::Size input_layer_size;
    cv::Size image_size;
    std::string pafs_blob_name;
    std::string heat_maps_blob_name;

    ResizeDeviceType maps_resize_device_type;
};
