/*******************************************************************************
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "peak.h"

#include <algorithm>
#include <utility>
#include <vector>

Peak::Peak(const int id, const cv::Point2f &pos, const float score) : id(id), pos(pos), score(score) {
}

HumanPoseByPeaksIndices::HumanPoseByPeaksIndices(const int keypoints_number, const int _peak_degree, const float _score)
    : peaks_indices(std::vector<int>(keypoints_number, -1)), peak_degree(_peak_degree), score(_score) {
}

TwoJointsConnection::TwoJointsConnection(const int first_joint_idx, const int second_joint_idx, const float score)
    : first_joint_idx(first_joint_idx), second_joint_idx(second_joint_idx), score(score) {
}

FindPeaksBody::FindPeaksBody(const std::vector<cv::Mat> &heat_maps, float min_peaks_distance,
                             std::vector<std::vector<Peak>> &peaks_from_heat_map)
    : heat_maps(heat_maps), min_peaks_distance(min_peaks_distance), peaks_from_heat_map(peaks_from_heat_map) {
}

void FindPeaksBody::operator()(const cv::Range &range) const {
    for (int i = range.start; i < range.end; i++) {
        findPeaks(heat_maps, min_peaks_distance, peaks_from_heat_map, i);
    }
}

std::vector<TwoJointsConnection> ComputeLineIntegralAndWeightedBipartiteGraph(
    const std::vector<Peak> &candidate_a, const std::vector<Peak> &candidate_b, const float mid_points_score_threshold,
    const std::pair<cv::Mat, cv::Mat> &score_mid, const std::vector<cv::Mat> &pafs,
    const float found_mid_points_ratio_threshold) {
    std::vector<TwoJointsConnection> temp_joint_connections;
    for (size_t i = 0; i < candidate_a.size(); ++i) {
        for (size_t j = 0; j < candidate_b.size(); ++j) {
            // building vectors
            cv::Point2f pt = candidate_a[i].pos * 0.5 + candidate_b[j].pos * 0.5;
            cv::Point mid = cv::Point(cvRound(pt.x), cvRound(pt.y));
            cv::Point2f vec = candidate_b[j].pos - candidate_a[i].pos;
            double norm_vec = cv::norm(vec);
            if (norm_vec == 0) {
                continue;
            }
            vec /= norm_vec;
            // sampling
            float score = vec.x * score_mid.first.at<float>(mid) + vec.y * score_mid.second.at<float>(mid);
            int height_n = pafs[0].rows / 2;
            float suc_ratio = 0.0f;
            float mid_score = 0.0f;
            const int mid_num = 10;
            const float score_threshold = -100.0f;
            if (score > score_threshold) {
                float p_sum = 0;
                int p_count = 0;
                cv::Size2f step((candidate_b[j].pos.x - candidate_a[i].pos.x) / (mid_num - 1),
                                (candidate_b[j].pos.y - candidate_a[i].pos.y) / (mid_num - 1));
                // evaluating on the fields
                for (int n = 0; n < mid_num; n++) {
                    cv::Point mid_point(cvRound(candidate_a[i].pos.x + n * step.width),
                                        cvRound(candidate_a[i].pos.y + n * step.height));
                    cv::Point2f pred(score_mid.first.at<float>(mid_point), score_mid.second.at<float>(mid_point));
                    // integral step
                    score = vec.x * pred.x + vec.y * pred.y;
                    if (score > mid_points_score_threshold) {
                        p_sum += score;
                        p_count++;
                    }
                }
                suc_ratio = static_cast<float>(p_count / mid_num);
                float ratio = p_count > 0 ? p_sum / p_count : 0.0f;
                mid_score = ratio + static_cast<float>(std::min(height_n / norm_vec - 1, 0.0));
            }
            // weighted bipartite graph
            if (mid_score > 0 && suc_ratio > found_mid_points_ratio_threshold) {
                temp_joint_connections.push_back(TwoJointsConnection(i, j, mid_score));
            }
        }
    }
    return temp_joint_connections;
}

void AssignmentAlgoritm(std::vector<TwoJointsConnection> &temp_joint_connections,
                        std::vector<TwoJointsConnection> &connections, const std::vector<Peak> &candidate_a,
                        const std::vector<Peak> &candidate_b) {
    // sort all possible cinnections from max to min
    std::sort(temp_joint_connections.begin(), temp_joint_connections.end(),
              [](const TwoJointsConnection &a, const TwoJointsConnection &b) { return (a.score > b.score); });

    int num_limbs = safe_convert<int>(std::min(candidate_a.size(), candidate_b.size()));
    int cnt = 0;
    std::vector<bool> occur_a(candidate_a.size(), false);
    std::vector<bool> occur_b(candidate_b.size(), false);
    for (size_t row = 0; row < temp_joint_connections.size(); row++) {
        // condition for exit, if all limb is connect
        if (cnt == num_limbs) {
            break;
        }
        const int &index_a = temp_joint_connections[row].first_joint_idx;
        const int &index_b = temp_joint_connections[row].second_joint_idx;
        const float &score = temp_joint_connections[row].score;
        // check not connected, if parts is not added
        // it is necessary connection
        if (!occur_a[index_a] && !occur_b[index_b]) {
            connections.push_back(TwoJointsConnection(candidate_a[index_a].id, candidate_b[index_b].id, score));
            cnt++;
            occur_a[index_a] = true;
            occur_b[index_b] = true;
        }
    }
}

void FillingSubSetForExistPeak(const size_t n_joint_peak, const size_t keypoints_number,
                               const std::vector<Peak> &candidate_peak, const int idx_joint_peak,
                               std::vector<HumanPoseByPeaksIndices> &pose_by_peak_indices_set) {

    for (size_t i = 0; i < n_joint_peak; ++i) {
        int num = 0;
        for (size_t j = 0; j < pose_by_peak_indices_set.size(); ++j) {
            if (pose_by_peak_indices_set[j].peaks_indices[idx_joint_peak] == candidate_peak[i].id) {
                num++;
                continue;
            }
        }
        if (num == 0) {
            HumanPoseByPeaksIndices person_keypoints(keypoints_number);
            // init the subsidiary struct with degree for
            // curr peak, id for myself and connected peak
            person_keypoints.peaks_indices[idx_joint_peak] = candidate_peak[i].id;
            person_keypoints.peak_degree = 1;
            person_keypoints.score = candidate_peak[i].score;
            pose_by_peak_indices_set.push_back(person_keypoints);
        }
    }
}

void FindPeaksBody::runNms(std::vector<cv::Point> &peaks, std::vector<std::vector<Peak>> &all_peaks, int heat_map_id,
                           const float min_peaks_distance, const cv::Mat &heat_map) const {
    std::sort(peaks.begin(), peaks.end(), [](const cv::Point &a, const cv::Point &b) { return a.x < b.x; });
    std::vector<bool> is_actual_peak(peaks.size(), true);
    int peak_counter = 0;
    std::vector<Peak> &peaks_with_score_and_id = all_peaks[heat_map_id];
    for (size_t i = 0; i < peaks.size(); ++i) {
        if (is_actual_peak[i]) {
            for (size_t j = i + 1; j < peaks.size(); ++j) {
                if (sqrt((peaks[i].x - peaks[j].x) * (peaks[i].x - peaks[j].x) +
                         (peaks[i].y - peaks[j].y) * (peaks[i].y - peaks[j].y)) < min_peaks_distance) {
                    is_actual_peak[j] = false;
                }
            }
            peaks_with_score_and_id.push_back(Peak(peak_counter++, peaks[i],
                                                   heat_map.at<float>(peaks[i]))); // each heatmap contain image
                                                                                   // with keypoint for specia
                                                                                   // limb(for all people on image)
        }
    }
}

void FindPeaksBody::findPeaks(const std::vector<cv::Mat> &heat_maps, const float min_peaks_distance,
                              std::vector<std::vector<Peak>> &all_peaks, int heat_map_id) const {
    const float threshold = 0.1f;
    std::vector<cv::Point> peaks;
    const cv::Mat &heat_map = heat_maps[heat_map_id];
    const float *heat_map_data = heat_map.ptr<float>();
    size_t heat_map_step = heat_map.step1();
    for (int y = -1; y < heat_map.rows + 1; y++) {
        for (int x = -1; x < heat_map.cols + 1; x++) {
            float val = 0;
            if (x >= 0 && y >= 0 && x < heat_map.cols && y < heat_map.rows) {
                val = heat_map_data[y * heat_map_step + x];
                val = val >= threshold ? val : 0;
            }

            float left_val = 0;
            if (y >= 0 && x < (heat_map.cols - 1) && y < heat_map.rows) {
                left_val = heat_map_data[y * heat_map_step + x + 1];
                left_val = left_val >= threshold ? left_val : 0;
            }

            float right_val = 0;
            if (x > 0 && y >= 0 && y < heat_map.rows) {
                right_val = heat_map_data[y * heat_map_step + x - 1];
                right_val = right_val >= threshold ? right_val : 0;
            }

            float top_val = 0;
            if (x >= 0 && x < heat_map.cols && y < (heat_map.rows - 1)) {
                top_val = heat_map_data[(y + 1) * heat_map_step + x];
                top_val = top_val >= threshold ? top_val : 0;
            }

            float bottom_val = 0;
            if (x >= 0 && y > 0 && x < heat_map.cols) {
                bottom_val = heat_map_data[(y - 1) * heat_map_step + x];
                bottom_val = bottom_val >= threshold ? bottom_val : 0;
            }

            if ((val > left_val) && (val > right_val) && (val > top_val) && (val > bottom_val)) {
                peaks.push_back(cv::Point(x, y));
            }
        }
    }
    runNms(peaks, all_peaks, heat_map_id, min_peaks_distance, heat_map);
}

void MergingTwoHumanPose(const std::vector<Peak> &candidates, const std::vector<TwoJointsConnection> &connections,
                         std::vector<HumanPoseByPeaksIndices> &pose_by_peak_indices_set, const size_t idx_heatmap_limb,
                         const int idx_joint_a, const int idx_joint_b, const size_t keypoints_number) {
    if (idx_heatmap_limb == 0) {
        pose_by_peak_indices_set =
            std::vector<HumanPoseByPeaksIndices>(connections.size(), HumanPoseByPeaksIndices(keypoints_number));
        for (size_t i = 0; i < connections.size(); ++i) {
            const int &index_a = connections[i].first_joint_idx;
            const int &index_b = connections[i].second_joint_idx;
            pose_by_peak_indices_set[i].peaks_indices[idx_joint_a] = index_a;
            pose_by_peak_indices_set[i].peaks_indices[idx_joint_b] = index_b;
            pose_by_peak_indices_set[i].peak_degree = 2;
            pose_by_peak_indices_set[i].score =
                candidates[index_a].score + candidates[index_b].score + connections[i].score;
        }
    } else {
        for (size_t i = 0; i < connections.size(); ++i) {
            const int &index_a = connections[i].first_joint_idx;
            const int &index_b = connections[i].second_joint_idx;
            bool num = false;
            for (size_t j = 0; j < pose_by_peak_indices_set.size(); ++j) {
                // if two humans share a part(same index and connection) merge the humans
                if (pose_by_peak_indices_set[j].peaks_indices[idx_joint_a] == index_a) {
                    // extend first humant id_limb from second human
                    pose_by_peak_indices_set[j].peaks_indices[idx_joint_b] = index_b;
                    pose_by_peak_indices_set[j].peak_degree++;
                    pose_by_peak_indices_set[j].score += candidates[index_b].score + connections[i].score;
                    num = true;
                }
            }
            if (!num) {
                // if not intersection, create and add new connection
                HumanPoseByPeaksIndices hp_with_score(keypoints_number);
                hp_with_score.peaks_indices[idx_joint_a] = index_a;
                hp_with_score.peaks_indices[idx_joint_b] = index_b;
                hp_with_score.peak_degree = 2;
                hp_with_score.score = candidates[index_a].score + candidates[index_b].score + connections[i].score;
                pose_by_peak_indices_set.push_back(hp_with_score);
            }
        }
    }
}

HumanPoseExtractor::HumanPoses GroupPeaksToPoses(const std::vector<std::vector<Peak>> &all_peaks,
                                                 const std::vector<cv::Mat> &pafs, const size_t keypoints_number,
                                                 const float mid_points_score_threshold,
                                                 const float found_mid_points_ratio_threshold,
                                                 const int min_peak_degree,
                                                 const float min_pose_by_peak_indices_set_score) {
    static const std::pair<int, int> limb_ids_heatmap[] = {{1, 2}, {1, 5},  {2, 3},   {3, 4},  {5, 6},   {6, 7},
                                                           {1, 8}, {8, 9},  {9, 10},  {1, 11}, {11, 12}, {12, 13},
                                                           {1, 0}, {0, 14}, {14, 16}, {0, 15}, {15, 17}};
    static const std::pair<int, int> limb_ids_paf[] = {{12, 13}, {20, 21}, {14, 15}, {16, 17}, {22, 23}, {24, 25},
                                                       {0, 1},   {2, 3},   {4, 5},   {6, 7},   {8, 9},   {10, 11},
                                                       {28, 29}, {30, 31}, {34, 35}, {32, 33}, {36, 37}};

    std::vector<Peak> candidates;
    for (const auto &peaks : all_peaks) {
        candidates.insert(candidates.end(), peaks.begin(), peaks.end());
    }
    std::vector<HumanPoseByPeaksIndices> pose_by_peak_indices_set;
    for (size_t k = 0; k < 17; k++) {
        std::vector<TwoJointsConnection> connections;
        std::pair<cv::Mat, cv::Mat> score_mid = {pafs[limb_ids_paf[k].first], pafs[limb_ids_paf[k].second]};
        const int idx_joint_a = limb_ids_heatmap[k].first;
        const int idx_joint_b = limb_ids_heatmap[k].second;
        const std::vector<Peak> &candidate_b = all_peaks[idx_joint_b]; // vector limbs, witch connect with
                                                                       // limbs from previous vector(ex. nose -
                                                                       // eye)
        const std::vector<Peak> &candidate_a = all_peaks[idx_joint_a];
        const size_t n_joints_a = candidate_a.size();
        const size_t n_joints_b = candidate_b.size();
        if (n_joints_a == 0 && n_joints_b == 0) {
            continue;
        } else if (n_joints_a == 0) { // it means that we have peak, which hasn't
                                      // connection with other from current subset
            FillingSubSetForExistPeak(n_joints_b, keypoints_number, candidate_b, idx_joint_b, pose_by_peak_indices_set);
            continue;
        } else if (n_joints_b == 0) {
            FillingSubSetForExistPeak(n_joints_a, keypoints_number, candidate_a, idx_joint_a, pose_by_peak_indices_set);
            continue;
        }
        std::vector<TwoJointsConnection> temp_joint_connections = ComputeLineIntegralAndWeightedBipartiteGraph(
            candidate_a, candidate_b, mid_points_score_threshold, score_mid, pafs, found_mid_points_ratio_threshold);
        if (!temp_joint_connections.empty()) {
            AssignmentAlgoritm(temp_joint_connections, connections, candidate_a, candidate_b);
        }
        if (connections.empty()) {
            continue;
        }

        MergingTwoHumanPose(candidates, connections, pose_by_peak_indices_set, k, idx_joint_a, idx_joint_b,
                            keypoints_number);
    }

    HumanPoseExtractor::HumanPoses poses;
    for (const auto &element_of_pose_by_peak_indices_set : pose_by_peak_indices_set) {
        if (element_of_pose_by_peak_indices_set.peak_degree < min_peak_degree ||
            element_of_pose_by_peak_indices_set.score / element_of_pose_by_peak_indices_set.peak_degree <
                min_pose_by_peak_indices_set_score) {
            continue;
        }
        int position = -1;
        HumanPoseExtractor::HumanPose pose(std::vector<cv::Point2f>(keypoints_number, cv::Point2f(-1.0f, -1.0f)),
                                           element_of_pose_by_peak_indices_set.score *
                                               std::max(0, element_of_pose_by_peak_indices_set.peak_degree - 1));
        for (const auto &peak_idx : element_of_pose_by_peak_indices_set.peaks_indices) {
            position++;
            if (peak_idx >= 0) {
                pose.keypoints[position] = candidates[peak_idx].pos;
                pose.keypoints[position].x += 0.5;
                pose.keypoints[position].y += 0.5;
            }
        }
        poses.push_back(pose);
    }
    return poses;
}
