/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "itracker.h"
#include <dlstreamer/base/memory_mapper.h>

#include <opencv2/opencv.hpp>

#include <deque>
#include <memory>
#include <vector>

namespace DeepSortWrapper {

// Deep SORT specific parameters
constexpr float DEFAULT_MAX_IOU_DISTANCE = 0.7f;    // Maximum IoU distance threshold for matching
constexpr int DEFAULT_MAX_AGE = 30;                 // Maximum number of missed frames before track is deleted
constexpr int DEFAULT_N_INIT = 3;                   // Number of consecutive hits required to confirm a track
constexpr float DEFAULT_MAX_COSINE_DISTANCE = 0.2f; // Maximum cosine distance for appearance matching.
constexpr int DEFAULT_NN_BUDGET = 100;
constexpr int DEFAULT_FEATURES_VECTOR_SIZE_128 = 128;

// Chi-square inverse 95% quantile for Mahalanobis gating
constexpr float CHI2INV95_2DOF = 5.9915f;  // 2 DOF (position only: x, y)
constexpr float INFTY_COST = 1e5f;

// Track states
enum class TrackState { Tentative = 1, Confirmed = 2, Deleted = 3 };

// Detection structure for Deep SORT
struct Detection {
    cv::Rect_<float> bbox;
    float confidence;
    std::vector<float> feature;
    int class_id;
    int region_index; // Original index into frame_meta.regions() for writing back track ID

    Detection(const cv::Rect_<float> &bbox, float confidence, const std::vector<float> &feature, int class_id = -1,
             int region_index = -1)
        : bbox(bbox), confidence(confidence), feature(feature), class_id(class_id), region_index(region_index) {
    }
};

// Track structure for Deep SORT
class Track {
  public:
    Track(const cv::Rect_<float> &bbox, int track_id, int n_init, int max_age, const std::vector<float> &feature,
          int nn_budget = DEFAULT_NN_BUDGET);

    void update(const Detection &detection);
    void predict();
    void mark_missed();
    bool is_tentative() const {
        return state_ == TrackState::Tentative;
    }
    bool is_confirmed() const {
        return state_ == TrackState::Confirmed;
    }
    bool is_deleted() const {
        return state_ == TrackState::Deleted;
    }

    cv::Rect_<float> to_bbox() const;
    int track_id() const {
        return track_id_;
    }
    int time_since_update() const {
        return time_since_update_;
    }

    // Feature management
    void add_feature(const std::vector<float> &feature);
    const std::deque<std::vector<float>> &features() const {
        return features_;
    }

    // Project state distribution to measurement space
    std::pair<cv::Mat, cv::Mat> project() const;

    // Compute squared Mahalanobis distances between state and measurements
    std::vector<float> gating_distance(const std::vector<Detection> &detections,
                                       const std::vector<int> &detection_indices,
                                       bool only_position = false) const;

    std::string state_str() const {
        switch (state_) {
        case TrackState::Tentative:
            return "Tentative";
        case TrackState::Confirmed:
            return "Confirmed";
        case TrackState::Deleted:
            return "Deleted";
        default:
            return "Unknown";
        }
    }

  private:
    // Kalman filter state - OpenCV implementation
    cv::Mat mean_;
    cv::Mat covariance_;

    int track_id_;
    int hits_;
    int age_;
    int time_since_update_;
    TrackState state_;

    // Parameters
    int n_init_;
    int max_age_;
    int nn_budget_;

    // Feature storage for cosine distance calculation
    std::deque<std::vector<float>> features_;

    void initiate(const cv::Rect_<float> &bbox);
};

// Re-ID gallery entry: stores info from a deleted track for re-identification
struct GalleryEntry {
    int track_id;
    std::deque<std::vector<float>> features;
    cv::Rect_<float> last_bbox;
    int deletion_frame;
};

// Deep SORT tracker implementation
class DeepSortTracker : public ITracker {
  public:
    // Constructor for using pre-extracted features from gvainference
    DeepSortTracker(float max_iou_distance = DEFAULT_MAX_IOU_DISTANCE, int max_age = DEFAULT_MAX_AGE,
                    int n_init = DEFAULT_N_INIT, float max_cosine_distance = DEFAULT_MAX_COSINE_DISTANCE,
                    int nn_budget = DEFAULT_NN_BUDGET, const std::string &dptrckcfg = "",
                    dlstreamer::MemoryMapperPtr mapper = nullptr);

    ~DeepSortTracker() override = default;

    void track(dlstreamer::FramePtr buffer, GVA::VideoFrame &frame_meta) override;

  private:
    // Deep SORT algorithm components
    std::vector<std::unique_ptr<Track>> tracks_;
    int next_id_;

    // Re-ID gallery: deleted tracks saved for re-identification
    std::vector<GalleryEntry> reid_gallery_;
    int reid_max_age_; // How long to keep gallery entries (frames). 0 = disabled.

    // Parameters
    float max_iou_distance_;
    int max_age_;
    int n_init_;
    float max_cosine_distance_;
    int nn_budget_;
    std::string dptrckcfg_;
    std::string object_class_; // Filter: only track detections with this label (empty = track all)

    // Memory mapper for buffer access
    dlstreamer::MemoryMapperPtr buffer_mapper_;

    // Helper methods
    std::vector<Detection> convert_detections(const std::vector<GVA::RegionOfInterest> &regions);
    void associate_detections_to_tracks(const std::vector<Detection> &detections,
                                        std::vector<std::pair<int, int>> &matches, std::vector<int> &unmatched_dets,
                                        std::vector<int> &unmatched_trks);
    float calculate_cosine_distance(const std::vector<float> &feat1, const std::vector<float> &feat2);
    float calculate_iou(const cv::Rect_<float> &bbox1, const cv::Rect_<float> &bbox2);

    // Hungarian algorithm for assignment
    void hungarian_assignment(const std::vector<std::vector<float>> &cost_matrix,
                              std::vector<std::pair<int, int>> &assignments);

    // Matching cascade for confirmed tracks (appearance-based + Mahalanobis gating)
    void matching_cascade(const std::vector<Detection> &detections,
                          const std::vector<int> &track_indices,
                          const std::vector<int> &detection_indices,
                          std::vector<std::pair<int, int>> &matches,
                          std::vector<int> &unmatched_tracks,
                          std::vector<int> &unmatched_detections);

    // IoU-based matching for second stage (unconfirmed + recently-missed tracks)
    void min_cost_matching_iou(const std::vector<Detection> &detections,
                               const std::vector<int> &track_indices,
                               const std::vector<int> &detection_indices,
                               std::vector<std::pair<int, int>> &matches,
                               std::vector<int> &unmatched_tracks,
                               std::vector<int> &unmatched_detections);

    // Gate cost matrix using Mahalanobis distance
    void gate_cost_matrix(std::vector<std::vector<float>> &cost_matrix,
                          const std::vector<Detection> &detections,
                          const std::vector<int> &track_indices,
                          const std::vector<int> &detection_indices);

    void parse_dps_trck_config();
};

} // namespace DeepSortWrapper