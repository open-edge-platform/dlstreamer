/*******************************************************************************
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "human_pose_extractor.h"
#include "peak.h"

#include <opencv2/imgproc/imgproc.hpp>

#include <algorithm>
#include <string>
#include <vector>

HumanPoseExtractor::HumanPoseExtractor(size_t keypoints_number, ResizeDeviceType maps_resize_device_type)
    : keypoints_number(keypoints_number), min_joints_number(3), stride(8), mean_pixel(cv::Vec3f::all(128)),
      min_peaks_distance(3.0f), mid_points_score_threshold(0.05f), found_mid_points_ratio_threshold(0.8f),
      min_subset_score(0.2f), upsample_ratio(4), maps_resize_device_type(maps_resize_device_type) {
}

HumanPoseExtractor::HumanPoses HumanPoseExtractor::postprocess(const float *heat_maps_data, const int heat_map_offset,
                                                               const int n_heat_maps, const float *pafs_data,
                                                               const int paf_offset, const int n_pafs,
                                                               const int feature_map_width,
                                                               const int feature_map_height) const {
    std::vector<cv::Mat> heat_maps(n_heat_maps);
    for (size_t i = 0; i < heat_maps.size(); i++) {
        heat_maps[i] = cv::Mat(feature_map_height, feature_map_width, CV_32FC1,
                               reinterpret_cast<void *>(const_cast<float *>(heat_maps_data + i * heat_map_offset)));
    }
    resizeFeatureMaps(heat_maps);

    std::vector<cv::Mat> pafs(n_pafs);
    for (size_t i = 0; i < pafs.size(); i++) {
        pafs[i] = cv::Mat(feature_map_height, feature_map_width, CV_32FC1,
                          reinterpret_cast<void *>(const_cast<float *>(pafs_data + i * paf_offset)));
    }
    resizeFeatureMaps(pafs);

    HumanPoseExtractor::HumanPoses poses = extractPoses(heat_maps, pafs);
    return poses;
}

HumanPoseExtractor::HumanPoses HumanPoseExtractor::extractPoses(const std::vector<cv::Mat> &heat_maps,
                                                                const std::vector<cv::Mat> &pafs) const {
    std::vector<std::vector<Peak>> peaks_from_heat_map(heat_maps.size());
    FindPeaksBody find_peaks_body(heat_maps, min_peaks_distance, peaks_from_heat_map);
    cv::parallel_for_(cv::Range(0, safe_convert<int>(heat_maps.size())), find_peaks_body);
    int peaks_before = 0;
    for (size_t heatmap_id = 1; heatmap_id < heat_maps.size(); heatmap_id++) {
        peaks_before += safe_convert<int>(peaks_from_heat_map[heatmap_id - 1].size());
        for (auto &peak : peaks_from_heat_map[heatmap_id]) {
            peak.id += peaks_before;
        }
    }
    HumanPoseExtractor::HumanPoses poses =
        GroupPeaksToPoses(peaks_from_heat_map, pafs, keypoints_number, mid_points_score_threshold,
                          found_mid_points_ratio_threshold, min_joints_number, min_subset_score);
    return poses;
}

void HumanPoseExtractor::resizeFeatureMaps(std::vector<cv::Mat> &feature_maps) const {
    switch (maps_resize_device_type) {
    case ResizeDeviceType::CPU_OCV:
        for (auto &feature_map : feature_maps) {
            cv::resize(feature_map, feature_map, cv::Size(), upsample_ratio, upsample_ratio, cv::INTER_CUBIC);
        }
        break;
    case ResizeDeviceType::GPU_OCV:
        for (auto &feature_map : feature_maps) {
            cv::UMat src = feature_map.getUMat(cv::ACCESS_READ);
            cv::UMat dst;
            cv::resize(src, dst, cv::Size(), upsample_ratio, upsample_ratio, cv::INTER_CUBIC);
            dst.copyTo(feature_map);
        }
        break;
    default:
        throw std::runtime_error("Unsupported device type.");
    }
}

void HumanPoseExtractor::correctCoordinates(HumanPoseExtractor::HumanPoses &poses,
                                            cv::Size output_feature_map_size) const {
    // output network image size
    cv::Size full_feature_map_size = output_feature_map_size * upsample_ratio;
    for (auto &pose : poses) {
        for (auto &keypoint : pose.keypoints) {
            if (keypoint != cv::Point2f(-1, -1)) {
                // transfer keypoint from output to original image
                keypoint.x /= static_cast<float>(full_feature_map_size.width);
                keypoint.y /= static_cast<float>(full_feature_map_size.height);
            }
        }
    }
}
