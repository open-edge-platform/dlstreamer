/*******************************************************************************
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "human_pose_extractor.h"

#include <opencv2/core/core.hpp>
#include <vector>

struct Peak {
    Peak(const int id = -1, const cv::Point2f &pos = cv::Point2f(), const float score = 0.0f);

    int id;
    cv::Point2f pos;
    float score;
};

struct HumanPoseByPeaksIndices {
    explicit HumanPoseByPeaksIndices(const int keypoints_number, const int _peak_degree = 0, const float _score = 0.0f);

    std::vector<int> peaks_indices;
    int peak_degree;
    float score;
};

struct TwoJointsConnection {
    TwoJointsConnection(const int first_joint_idx, const int second_joint_idx, const float score);

    int first_joint_idx;
    int second_joint_idx;
    float score;
};

HumanPoseExtractor::HumanPoses GroupPeaksToPoses(const std::vector<std::vector<Peak>> &all_peaks,
                                                 const std::vector<cv::Mat> &pafs, const size_t keypoints_number,
                                                 const float mid_points_score_threshold,
                                                 const float found_mid_points_ratio_threshold,
                                                 const int min_joints_number, const float min_subset_score);

void MergingTwoHumanPose(const std::vector<Peak> &candidates, const std::vector<TwoJointsConnection> &connections,
                         std::vector<HumanPoseByPeaksIndices> &pose_by_peak_indices_set, const size_t idx_heatmap_limb,
                         const int idx_joint_a, const int idx_joint_b, const size_t keypoints_number);

void FillingSubSetForExistPeak(const size_t n_joint_peak, const size_t keypoints_number,
                               const std::vector<Peak> &candidate_peak, const int idx_joint_peak,
                               std::vector<HumanPoseByPeaksIndices> &pose_by_peak_indices_set);

void AssignmentAlgoritm(std::vector<TwoJointsConnection> &temp_joint_connections,
                        std::vector<TwoJointsConnection> &connections, const std::vector<Peak> &candidate_a,
                        const std::vector<Peak> &candidate_b);

std::vector<TwoJointsConnection> ComputeLineIntegralAndWeightedBipartiteGraph(
    const std::vector<Peak> &candidate_a, const std::vector<Peak> &candidate_b, const float mid_points_score_threshold,
    const std::pair<cv::Mat, cv::Mat> &score_mid, const std::vector<cv::Mat> &pafs,
    const float found_mid_points_ratio_threshold);

class FindPeaksBody : public cv::ParallelLoopBody {
  public:
    FindPeaksBody(const std::vector<cv::Mat> &heat_maps, float min_peaks_distance,
                  std::vector<std::vector<Peak>> &peaks_from_heat_map);

    void operator()(const cv::Range &range) const;

    void runNms(std::vector<cv::Point> &peaks, std::vector<std::vector<Peak>> &all_peaks, int heat_map_id,
                const float min_peaks_distance, const cv::Mat &heat_map) const;

    void findPeaks(const std::vector<cv::Mat> &heat_maps, const float min_peaks_distance,
                   std::vector<std::vector<Peak>> &all_peaks, int heat_map_id) const;

  private:
    const std::vector<cv::Mat> &heat_maps;
    float min_peaks_distance;
    std::vector<std::vector<Peak>> &peaks_from_heat_map;
};
