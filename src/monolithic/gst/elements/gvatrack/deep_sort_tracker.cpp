/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/*
 * Deep SORT Tracker — Algorithm Flow
 *
 * Pipeline context:
 *   decodebin --> gvadetect (YOLO) --> gvainference (mars-small128) --> gvatrack (this file)
 *
 * Per-frame processing (inside gvatrack):
 *
 *   1. Kalman Predict — advance all tracks one time step
 *
 *   2. Stage 1: Matching Cascade (confirmed tracks only)
 *      - Iterate cascade levels by time_since_update (recently-seen first)
 *      - Cost: nearest-neighbor cosine distance on appearance features
 *      - Gates: Mahalanobis (Kalman), TSU-scaled spatial, proximity competition
 *      - Assignment: Hungarian algorithm
 *      - Outputs: matched pairs, leftover detections, leftover tracks
 *
 *   3. Stage 2: IoU Matching
 *      - Candidates: unconfirmed tracks + confirmed tracks missed once (tsu==1)
 *      - Cost: 1 - IoU (bounding box overlap)
 *      - Assignment: Hungarian algorithm
 *      - Outputs: matched pairs, leftover detections, leftover tracks
 *
 *   4. Post-processing:
 *      - Matched tracks   --> Kalman update with detection
 *      - Unmatched tracks --> mark_missed; if deleted, save to re-ID gallery
 *      - Unmatched dets   --> check re-ID gallery; create track (reused or new ID)
 */

#include "deep_sort_tracker.h"
#include "utils.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <sstream>

GST_DEBUG_CATEGORY_STATIC(deep_sort_debug);
#define GST_CAT_DEFAULT deep_sort_debug

static void __attribute__((constructor)) init_debug_category(void) {
    GST_DEBUG_CATEGORY_INIT(deep_sort_debug, "deepsort", 0, "Deep SORT tracker");
}

namespace DeepSortWrapper {

// Track implementation

/**
 * @brief Constructs a new Track with initial detection data and Kalman filter state
 */
Track::Track(const cv::Rect_<float> &bbox, int track_id, int n_init, int max_age, const std::vector<float> &feature,
             int nn_budget)
    : track_id_(track_id), hits_(1), age_(1), time_since_update_(0), state_(TrackState::Tentative), n_init_(n_init),
      max_age_(max_age), nn_budget_(nn_budget) {
    initiate(bbox);
    add_feature(feature);
}

/**
 * @brief Initialize Kalman filter state and covariance matrix from first detection bbox
 */
void Track::initiate(const cv::Rect_<float> &bbox) {
    // Initialize Kalman filter with 8-dimensional state space (x, y, aspect_ratio, height, vx, vy, va, vh)
    mean_ = cv::Mat::zeros(8, 1, CV_32F);
    mean_.at<float>(0) = bbox.x + bbox.width / 2.0f;  // center_x
    mean_.at<float>(1) = bbox.y + bbox.height / 2.0f; // center_y
    mean_.at<float>(2) = bbox.width / bbox.height;    // aspect_ratio
    mean_.at<float>(3) = bbox.height;                 // height

    // Initialize covariance: variance = std^2
    covariance_ = cv::Mat::zeros(8, 8, CV_32F);
    float std_weight_position = 1.0f / 20.0f;
    float std_weight_velocity = 1.0f / 160.0f;
    float h = bbox.height;

    float s0 = 2.0f * std_weight_position * h;
    float s1 = 2.0f * std_weight_position * h;
    float s2 = 1e-2f;
    float s3 = 2.0f * std_weight_position * h;
    float s4 = 10.0f * std_weight_velocity * h;
    float s5 = 10.0f * std_weight_velocity * h;
    float s6 = 1e-5f;
    float s7 = 10.0f * std_weight_velocity * h;

    covariance_.at<float>(0, 0) = s0 * s0;
    covariance_.at<float>(1, 1) = s1 * s1;
    covariance_.at<float>(2, 2) = s2 * s2;
    covariance_.at<float>(3, 3) = s3 * s3;
    covariance_.at<float>(4, 4) = s4 * s4;
    covariance_.at<float>(5, 5) = s5 * s5;
    covariance_.at<float>(6, 6) = s6 * s6;
    covariance_.at<float>(7, 7) = s7 * s7;
}

/**
 * @brief Predict next state using Kalman filter motion model (constant velocity)
 */
void Track::predict() {
    // State transition matrix (constant velocity model)
    cv::Mat F = cv::Mat::eye(8, 8, CV_32F);
    F.at<float>(0, 4) = 1.0f; // x += vx
    F.at<float>(1, 5) = 1.0f; // y += vy
    F.at<float>(2, 6) = 1.0f; // aspect_ratio += va
    F.at<float>(3, 7) = 1.0f; // height += vh

    mean_ = F * mean_;

    // Process noise: Q = diag(std^2)
    cv::Mat Q = cv::Mat::zeros(8, 8, CV_32F);
    float std_weight_position = 1.0f / 20.0f;
    float std_weight_velocity = 1.0f / 160.0f;
    float height = mean_.at<float>(3);

    float sp0 = std_weight_position * height;
    float sp2 = 1e-2f;
    float sv0 = std_weight_velocity * height;
    float sv2 = 1e-5f;

    Q.at<float>(0, 0) = sp0 * sp0;
    Q.at<float>(1, 1) = sp0 * sp0;
    Q.at<float>(2, 2) = sp2 * sp2;
    Q.at<float>(3, 3) = sp0 * sp0;
    Q.at<float>(4, 4) = sv0 * sv0;
    Q.at<float>(5, 5) = sv0 * sv0;
    Q.at<float>(6, 6) = sv2 * sv2;
    Q.at<float>(7, 7) = sv0 * sv0;

    // Update covariance: P = F*P*F' + Q
    covariance_ = F * covariance_ * F.t() + Q;

    // Increment counters
    age_++;
    time_since_update_++;
}

/**
 * @brief Update track state with matched detection using Kalman filter correction step
 */
void Track::update(const Detection &detection) {
    // Measurement model
    cv::Mat H = cv::Mat::zeros(4, 8, CV_32F);
    H.at<float>(0, 0) = 1.0f;
    H.at<float>(1, 1) = 1.0f;
    H.at<float>(2, 2) = 1.0f;
    H.at<float>(3, 3) = 1.0f;

    // Measurement noise: R = diag(std^2)
    cv::Mat R = cv::Mat::zeros(4, 4, CV_32F);
    float std_weight_position = 1.0f / 20.0f;
    float height = mean_.at<float>(3); // Use predicted state height (not detection height)

    float sp = std_weight_position * height;
    float sa = 1e-1f;
    R.at<float>(0, 0) = sp * sp;
    R.at<float>(1, 1) = sp * sp;
    R.at<float>(2, 2) = sa * sa;
    R.at<float>(3, 3) = sp * sp;

    cv::Mat z = cv::Mat::zeros(4, 1, CV_32F);
    z.at<float>(0) = detection.bbox.x + detection.bbox.width / 2.0f;
    z.at<float>(1) = detection.bbox.y + detection.bbox.height / 2.0f;
    z.at<float>(2) = detection.bbox.width / detection.bbox.height;
    z.at<float>(3) = detection.bbox.height;

    cv::Mat S = H * covariance_ * H.t() + R;
    cv::Mat K = covariance_ * H.t() * S.inv();
    cv::Mat y = z - H * mean_;

    mean_ = mean_ + K * y;
    // Joseph form for numerical stability: (I-KH)*P can lose symmetry over many frames
    covariance_ = covariance_ - K * S * K.t();
    // Enforce symmetry to prevent numerical drift
    covariance_ = (covariance_ + covariance_.t()) * 0.5f;

    add_feature(detection.feature);

    hits_++;
    time_since_update_ = 0;

    if (state_ == TrackState::Tentative && hits_ >= n_init_) {
        state_ = TrackState::Confirmed;
    }
}

/**
 * @brief Mark track as missed (no detection match) and update state/age counters
 */
void Track::mark_missed() {
    if (state_ == TrackState::Tentative) {
        // Tentative tracks are immediately deleted on miss
        state_ = TrackState::Deleted;
    } else if (time_since_update_ > max_age_) {
        state_ = TrackState::Deleted;
    }
}

/**
 * @brief Convert Kalman filter state to bounding box
 */
cv::Rect_<float> Track::to_bbox() const {
    float center_x = mean_.at<float>(0);
    float center_y = mean_.at<float>(1);
    float aspect_ratio = mean_.at<float>(2);
    float height = mean_.at<float>(3);
    float width = aspect_ratio * height;

    return cv::Rect_<float>(center_x - width / 2.0f, center_y - height / 2.0f, width, height);
}

/**
 * @brief Add new feature vector to track's feature history (with budget limit)
 */
void Track::add_feature(const std::vector<float> &feature) {
    features_.push_back(feature);
    if (nn_budget_ > 0 && features_.size() > static_cast<size_t>(nn_budget_)) {
        features_.pop_front();
    }
}

/**
 * @brief Project state distribution to measurement space
 */
std::pair<cv::Mat, cv::Mat> Track::project() const {
    cv::Mat H = cv::Mat::zeros(4, 8, CV_32F);
    H.at<float>(0, 0) = 1.0f;
    H.at<float>(1, 1) = 1.0f;
    H.at<float>(2, 2) = 1.0f;
    H.at<float>(3, 3) = 1.0f;

    float std_weight_position = 1.0f / 20.0f;
    float height = mean_.at<float>(3);

    float sp = std_weight_position * height;
    float sa = 1e-1f;

    cv::Mat R = cv::Mat::zeros(4, 4, CV_32F);
    R.at<float>(0, 0) = sp * sp;
    R.at<float>(1, 1) = sp * sp;
    R.at<float>(2, 2) = sa * sa;
    R.at<float>(3, 3) = sp * sp;

    cv::Mat projected_mean = H * mean_;
    cv::Mat projected_cov = H * covariance_ * H.t() + R;

    return {projected_mean, projected_cov};
}

/**
 * @brief Compute squared Mahalanobis distances between track state and detections
 */
std::vector<float> Track::gating_distance(const std::vector<Detection> &detections,
                                          const std::vector<int> &detection_indices, bool only_position) const {
    auto [proj_mean, proj_cov] = project();

    int dim = only_position ? 2 : 4;
    cv::Mat mean = proj_mean(cv::Range(0, dim), cv::Range::all());
    cv::Mat cov = proj_cov(cv::Range(0, dim), cv::Range(0, dim));

    // Cholesky solve: avoids full inverse, more numerically stable

    std::vector<float> distances(detection_indices.size());
    for (size_t i = 0; i < detection_indices.size(); ++i) {
        int det_idx = detection_indices[i];
        cv::Mat z = cv::Mat::zeros(dim, 1, CV_32F);
        z.at<float>(0) = detections[det_idx].bbox.x + detections[det_idx].bbox.width / 2.0f;
        z.at<float>(1) = detections[det_idx].bbox.y + detections[det_idx].bbox.height / 2.0f;
        if (!only_position) {
            z.at<float>(2) = detections[det_idx].bbox.width / detections[det_idx].bbox.height;
            z.at<float>(3) = detections[det_idx].bbox.height;
        }

        cv::Mat diff = z - mean;
        cv::Mat x;
        bool solved = cv::solve(cov, diff, x, cv::DECOMP_CHOLESKY);
        if (!solved) {
            // Fallback to SVD if Cholesky fails (non-positive-definite)
            cv::solve(cov, diff, x, cv::DECOMP_SVD);
        }
        cv::Mat result = diff.t() * x;
        distances[i] = result.at<float>(0, 0);
    }
    return distances;
}

// DeepSortTracker implementation

/**
 * @brief Initialize Deep SORT tracker with tracking parameters (features from gvainference)
 */
DeepSortTracker::DeepSortTracker(float max_iou_distance, int max_age, int n_init, float max_cosine_distance,
                                 int nn_budget, const std::string &dptrckcfg, dlstreamer::MemoryMapperPtr mapper)
    : next_id_(1), reid_max_age_(0), max_iou_distance_(max_iou_distance), max_age_(max_age), n_init_(n_init),
      max_cosine_distance_(max_cosine_distance), nn_budget_(nn_budget), dptrckcfg_(dptrckcfg),
      buffer_mapper_(std::move(mapper)) {

    parse_dps_trck_config();
    GST_INFO("DeepSortTracker: max_iou_distance=%.3f, max_age=%d, n_init=%d, "
             "max_cosine_distance=%.3f, nn_budget=%d, object_class=%s, reid_max_age=%d",
             max_iou_distance_, max_age_, n_init_, max_cosine_distance_, nn_budget_,
             object_class_.empty() ? "(all)" : object_class_.c_str(), reid_max_age_);
}

/**
 * @brief Main tracking function - process frame detections and update tracks with IDs
 */
void DeepSortTracker::track(dlstreamer::FramePtr buffer, GVA::VideoFrame &frame_meta) {
    if (!buffer) {
        throw std::invalid_argument("DeepSortTracker: buffer is nullptr");
    }
    static int frame_num_ = 0;
    frame_num_++;

    // Expire old re-ID gallery entries
    if (reid_max_age_ > 0) {
        reid_gallery_.erase(
            std::remove_if(reid_gallery_.begin(), reid_gallery_.end(),
                           [&](const GalleryEntry &e) { return (frame_num_ - e.deletion_frame) > reid_max_age_; }),
            reid_gallery_.end());
    }

    auto regions = frame_meta.regions();

    std::vector<Detection> detections = convert_detections(regions);

    {
        std::ostringstream oss;
        oss << "F" << frame_num_ << " DETECTIONS: " << detections.size() << " from " << regions.size() << " regions";
        for (size_t d = 0; d < detections.size(); ++d) {
            oss << " | det[" << d << "] reg=" << detections[d].region_index << " bbox[" << (int)detections[d].bbox.x
                << "," << (int)detections[d].bbox.y << "," << (int)detections[d].bbox.width << ","
                << (int)detections[d].bbox.height << "]";
        }
        GST_DEBUG("%s", oss.str().c_str());
    }

    // Step 1: Predict all tracks forward
    for (auto &track : tracks_) {
        track->predict();
    }

    // Step 2: Associate detections to tracks
    std::vector<std::pair<int, int>> matches;
    std::vector<int> unmatched_dets, unmatched_trks;
    associate_detections_to_tracks(detections, matches, unmatched_dets, unmatched_trks);

    // Step 3: Update matched tracks and assign object IDs
    for (const auto &match : matches) {
        tracks_[match.second]->update(detections[match.first]);

        auto &detection = detections[match.first];
        auto &track = tracks_[match.second];

        GST_DEBUG("F%d MATCH: det[%d](reg=%d) bbox[%.0f,%.0f] -> track_id=%d, state=%s", frame_num_, match.first,
                  detection.region_index, detection.bbox.x + detection.bbox.width / 2.0f,
                  detection.bbox.y + detection.bbox.height / 2.0f, track->track_id(), track->state_str().c_str());

        // Assign tracking ID to the original region
        int reg_idx = detections[match.first].region_index;
        if (reg_idx >= 0 && reg_idx < static_cast<int>(regions.size()) && tracks_[match.second]->is_confirmed()) {
            regions[reg_idx].set_object_id(tracks_[match.second]->track_id());
        }
    }

    // Mark unmatched tracks as missed
    for (int trk_idx : unmatched_trks) {
        if (tracks_[trk_idx]->is_confirmed()) {
            GST_DEBUG("CONFIRMED TRACK UNMATCHED: track_id=%d, tsu=%d", tracks_[trk_idx]->track_id(),
                      tracks_[trk_idx]->time_since_update());
        }
        tracks_[trk_idx]->mark_missed();
    }

    // Step 5: Create new tracks for unmatched detections
    for (int det_idx : unmatched_dets) {
        int track_id = next_id_;
        bool reused = false;

        // Check re-ID gallery for a matching deleted track
        if (reid_max_age_ > 0 && !reid_gallery_.empty()) {
            float best_dist = max_cosine_distance_;
            int best_gallery_idx = -1;

            for (size_t g = 0; g < reid_gallery_.size(); ++g) {
                // Cosine distance: min over gallery features (nn-distance)
                float min_dist = INFTY_COST;
                for (const auto &gfeat : reid_gallery_[g].features) {
                    float dist = calculate_cosine_distance(detections[det_idx].feature, gfeat);
                    min_dist = std::min(min_dist, dist);
                }

                if (min_dist < best_dist) {
                    // Spatial sanity check: max 50 pixels/frame movement
                    float det_cx = detections[det_idx].bbox.x + detections[det_idx].bbox.width / 2.0f;
                    float det_cy = detections[det_idx].bbox.y + detections[det_idx].bbox.height / 2.0f;
                    float gal_cx = reid_gallery_[g].last_bbox.x + reid_gallery_[g].last_bbox.width / 2.0f;
                    float gal_cy = reid_gallery_[g].last_bbox.y + reid_gallery_[g].last_bbox.height / 2.0f;
                    float dx = det_cx - gal_cx;
                    float dy = det_cy - gal_cy;
                    float spatial_dist = std::sqrt(dx * dx + dy * dy);
                    int dt = frame_num_ - reid_gallery_[g].deletion_frame;
                    float max_spatial = 50.0f * std::max(dt, 1);

                    if (spatial_dist <= max_spatial) {
                        best_dist = min_dist;
                        best_gallery_idx = static_cast<int>(g);
                    }
                }
            }

            if (best_gallery_idx >= 0) {
                track_id = reid_gallery_[best_gallery_idx].track_id;
                GST_DEBUG("F%d RE-ID: det[%d] bbox[%.0f,%.0f] matched gallery track_id=%d (cosine=%.4f)", frame_num_,
                          det_idx, detections[det_idx].bbox.x + detections[det_idx].bbox.width / 2.0f,
                          detections[det_idx].bbox.y + detections[det_idx].bbox.height / 2.0f, track_id, best_dist);
                reid_gallery_.erase(reid_gallery_.begin() + best_gallery_idx);
                reused = true;
            }
        }

        if (!reused)
            track_id = next_id_++;

        auto new_track = std::make_unique<Track>(detections[det_idx].bbox, track_id, n_init_, max_age_,
                                                 detections[det_idx].feature, nn_budget_);
        GST_DEBUG("NEW TRACK: ID=%d%s, bbox[%.1f, %.1f, %.1f x %.1f]", track_id, reused ? " (RE-ID)" : "",
                  detections[det_idx].bbox.x, detections[det_idx].bbox.y, detections[det_idx].bbox.width,
                  detections[det_idx].bbox.height);
        tracks_.push_back(std::move(new_track));
    }

    // Step 6: Save deleted confirmed tracks to re-ID gallery, then remove
    if (reid_max_age_ > 0) {
        for (auto &track : tracks_) {
            if (track->is_deleted() && !track->features().empty()) {
                GalleryEntry entry;
                entry.track_id = track->track_id();
                entry.features = track->features();
                entry.last_bbox = track->to_bbox();
                entry.deletion_frame = frame_num_;
                GST_DEBUG("F%d GALLERY SAVE: track_id=%d, features=%zu, bbox[%.0f,%.0f]", frame_num_, entry.track_id,
                          entry.features.size(), entry.last_bbox.x + entry.last_bbox.width / 2.0f,
                          entry.last_bbox.y + entry.last_bbox.height / 2.0f);
                reid_gallery_.push_back(std::move(entry));
            }
        }
    }
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                                 [](const std::unique_ptr<Track> &track) { return track->is_deleted(); }),
                  tracks_.end());
}

/**
 * @brief Convert GVA region detections to Deep SORT Detection objects
 */
std::vector<Detection> DeepSortTracker::convert_detections(const std::vector<GVA::RegionOfInterest> &regions) {

    std::vector<Detection> detections;
    detections.reserve(regions.size());

    // Extract features from tensor data (attached by gvainference)
    for (size_t i = 0; i < regions.size(); ++i) {
        const auto &region = regions[i];
        std::string label = region.label();
        // Filter by object class if configured
        if (!object_class_.empty() && label != object_class_) {
            continue;
        }

        cv::Rect_<float> bbox(region.rect().x, region.rect().y, region.rect().w, region.rect().h);
        float confidence = region.confidence();

        // Extract feature vector from tensor data
        std::vector<float> feature_vector;
        bool found_feature = false;

        auto tensors = region.tensors();
        for (const auto &tensor : tensors) {
            std::string tensor_name = tensor.name();
            std::string layer_name = tensor.layer_name();

            // Match feature tensor by layer name
            if (((layer_name.find("output") != std::string::npos &&
                  tensor_name.find("inference_layer_name:output") != std::string::npos)) ||
                ((layer_name.find("features") != std::string::npos) &&
                 (tensor_name.find("inference_layer_name:features") != std::string::npos))) {

                feature_vector = tensor.data<float>();

                if (!feature_vector.empty() && feature_vector.size() == DEFAULT_FEATURES_VECTOR_SIZE_128) {
                    // L2 normalize
                    float norm = std::sqrt(
                        std::inner_product(feature_vector.begin(), feature_vector.end(), feature_vector.begin(), 0.0f));
                    if (norm > 0.0f) {
                        for (float &f : feature_vector) {
                            f /= norm;
                        }
                    }
                    found_feature = true;
                    break;
                }
            }
        }

        // If no feature found, use zero vector (will disable appearance-based matching)
        if (!found_feature) {
            GST_WARNING("No feature tensor found for region %zu, using zero feature (motion-only tracking)", i);
            feature_vector =
                std::vector<float>(DEFAULT_FEATURES_VECTOR_SIZE_128, 0.0f); // Default 128-dimensional zero vector
        }

        GST_DEBUG("{%s} Detection %zu (gvainference): bbox[%d,%d,%d,%d], confidence=%.3f, feature_size=%zu",
                  __FUNCTION__, i, (int)bbox.x, (int)bbox.y, (int)bbox.width, (int)bbox.height, confidence,
                  feature_vector.size());

        detections.emplace_back(bbox, confidence, feature_vector, -1, static_cast<int>(i));
    }

    return detections;
}

/**
 * @brief Associate current detections with existing tracks using Deep SORT's two-stage matching cascade.
 *
 * Stage 1: Confirmed tracks matched by appearance (cosine distance) with Mahalanobis gating,
 *          using a cascade that prioritizes recently-seen tracks.
 * Stage 2: Unconfirmed tracks + recently-missed confirmed tracks matched by IoU distance.
 *
 */
void DeepSortTracker::associate_detections_to_tracks(const std::vector<Detection> &detections,
                                                     std::vector<std::pair<int, int>> &matches,
                                                     std::vector<int> &unmatched_dets,
                                                     std::vector<int> &unmatched_trks) {
    matches.clear();
    unmatched_dets.clear();
    unmatched_trks.clear();

    if (tracks_.empty()) {
        for (size_t i = 0; i < detections.size(); ++i) {
            unmatched_dets.push_back(i);
        }
        return;
    }

    // Split tracks into confirmed and unconfirmed
    std::vector<int> confirmed_tracks, unconfirmed_tracks;
    for (size_t i = 0; i < tracks_.size(); ++i) {
        if (tracks_[i]->is_confirmed()) {
            confirmed_tracks.push_back(static_cast<int>(i));
        } else {
            unconfirmed_tracks.push_back(static_cast<int>(i));
        }
    }

    // All detection indices
    std::vector<int> all_det_indices;
    for (size_t i = 0; i < detections.size(); ++i) {
        all_det_indices.push_back(static_cast<int>(i));
    }

    // Stage 1: Matching cascade for confirmed tracks (appearance + Mahalanobis gating)
    std::vector<std::pair<int, int>> matches_a;
    std::vector<int> unmatched_tracks_a;
    std::vector<int> unmatched_detections_after_cascade;

    matching_cascade(detections, confirmed_tracks, all_det_indices, matches_a, unmatched_tracks_a,
                     unmatched_detections_after_cascade);

    // Stage 2: IoU matching for unconfirmed + recently-missed confirmed tracks (tsu==1)
    std::vector<int> iou_track_candidates = unconfirmed_tracks;
    std::vector<int> remaining_unmatched_tracks_a;

    for (int k : unmatched_tracks_a) {
        if (tracks_[k]->time_since_update() == 1) {
            iou_track_candidates.push_back(k);
        } else {
            GST_DEBUG("CONFIRMED TRACK EXCLUDED from IoU fallback: track_id=%d, tsu=%d", tracks_[k]->track_id(),
                      tracks_[k]->time_since_update());
            remaining_unmatched_tracks_a.push_back(k);
        }
    }

    std::vector<std::pair<int, int>> matches_b;
    std::vector<int> unmatched_tracks_b;
    min_cost_matching_iou(detections, iou_track_candidates, unmatched_detections_after_cascade, matches_b,
                          unmatched_tracks_b, unmatched_dets);

    // Combine results from both stages
    matches = matches_a;
    matches.insert(matches.end(), matches_b.begin(), matches_b.end());

    // Unmatched tracks = remaining from stage 1 + unmatched from stage 2
    unmatched_trks = remaining_unmatched_tracks_a;
    unmatched_trks.insert(unmatched_trks.end(), unmatched_tracks_b.begin(), unmatched_tracks_b.end());
}

/**
 * @brief Matching cascade: prioritize recently-seen tracks for appearance-based matching
 */
void DeepSortTracker::matching_cascade(const std::vector<Detection> &detections, const std::vector<int> &track_indices,
                                       const std::vector<int> &detection_indices,
                                       std::vector<std::pair<int, int>> &matches, std::vector<int> &unmatched_tracks,
                                       std::vector<int> &unmatched_detections) {
    matches.clear();
    unmatched_detections = detection_indices;

    for (int level = 0; level < max_age_; ++level) {
        if (unmatched_detections.empty())
            break;

        // Find tracks at this cascade level (time_since_update == 1 + level after predict)
        std::vector<int> track_indices_l;
        for (int k : track_indices) {
            if (tracks_[k]->time_since_update() == 1 + level) {
                track_indices_l.push_back(k);
            }
        }
        if (track_indices_l.empty())
            continue;

        // Build cost matrix: rows=tracks, cols=detections
        size_t n_tracks = track_indices_l.size();
        size_t n_dets = unmatched_detections.size();
        std::vector<std::vector<float>> cost_matrix(n_tracks, std::vector<float>(n_dets));

        for (size_t row = 0; row < n_tracks; ++row) {
            int trk_idx = track_indices_l[row];
            const auto &track_features = tracks_[trk_idx]->features();

            for (size_t col = 0; col < n_dets; ++col) {
                int det_idx = unmatched_detections[col];

                // Compute nearest-neighbor cosine distance (min across stored features)
                float min_cosine_dist = 1.0f;
                for (const auto &track_feature : track_features) {
                    float dist = calculate_cosine_distance(detections[det_idx].feature, track_feature);
                    min_cosine_dist = std::min(min_cosine_dist, dist);
                }
                cost_matrix[row][col] = min_cosine_dist;
            }
        }

        // Apply Mahalanobis gating
        gate_cost_matrix(cost_matrix, detections, track_indices_l, unmatched_detections);

        // Gate by max_cosine_distance threshold
        for (size_t row = 0; row < n_tracks; ++row) {
            for (size_t col = 0; col < n_dets; ++col) {
                if (cost_matrix[row][col] > max_cosine_distance_) {
                    cost_matrix[row][col] = max_cosine_distance_ + 1e-5f;
                }
            }
        }

        // Solve optimal assignment using Hungarian algorithm
        std::vector<std::pair<int, int>> assignments;
        hungarian_assignment(cost_matrix, assignments);

        // Process assignments: filter by threshold, map back to original indices
        std::vector<bool> det_matched(n_dets, false);
        for (const auto &[row, col] : assignments) {
            if (cost_matrix[row][col] <= max_cosine_distance_) {
                int trk_idx_match = track_indices_l[row];
                int det_idx_match = unmatched_detections[col];

                // Proximity competition check: prevent stale tracks from stealing detections
                // that clearly belong to even staler (higher tsu) unmatched tracks.
                // Only applies at cascade level > 0 (stale tracks).
                bool blocked_by_competition = false;
                if (level > 0) {
                    const cv::Rect_<float> &det_bb = detections[det_idx_match].bbox;
                    float d_cx = det_bb.x + det_bb.width / 2.0f;
                    float d_cy = det_bb.y + det_bb.height / 2.0f;

                    cv::Rect_<float> m_bb = tracks_[trk_idx_match]->to_bbox();
                    float m_cx = m_bb.x + m_bb.width / 2.0f;
                    float m_cy = m_bb.y + m_bb.height / 2.0f;
                    float m_dx = m_cx - d_cx;
                    float m_dy = m_cy - d_cy;
                    float match_dist_sq = m_dx * m_dx + m_dy * m_dy;

                    // Check all confirmed tracks at higher cascade levels (staler, not yet processed)
                    for (int k : track_indices) {
                        if (k == trk_idx_match)
                            continue;
                        // Only check staler tracks that haven't been matched yet
                        if (tracks_[k]->time_since_update() <= tracks_[trk_idx_match]->time_since_update())
                            continue;
                        bool already_matched = false;
                        for (const auto &prev_match : matches) {
                            if (prev_match.second == k) {
                                already_matched = true;
                                break;
                            }
                        }
                        if (already_matched)
                            continue;

                        cv::Rect_<float> c_bb = tracks_[k]->to_bbox();
                        float c_cx = c_bb.x + c_bb.width / 2.0f;
                        float c_cy = c_bb.y + c_bb.height / 2.0f;
                        float c_dx = c_cx - d_cx;
                        float c_dy = c_cy - d_cy;
                        float comp_dist_sq = c_dx * c_dx + c_dy * c_dy;

                        // Block if competitor is at least 2x closer (dist_sq ratio < 0.25)
                        if (comp_dist_sq < match_dist_sq * 0.25f) {
                            GST_DEBUG("PROXIMITY BLOCK: track_id=%d(tsu=%d) blocked from det[%d] — "
                                      "track_id=%d(tsu=%d) is %.1fx closer (%.0f vs %.0f px)",
                                      tracks_[trk_idx_match]->track_id(), tracks_[trk_idx_match]->time_since_update(),
                                      det_idx_match, tracks_[k]->track_id(), tracks_[k]->time_since_update(),
                                      std::sqrt(match_dist_sq / std::max(comp_dist_sq, 1.0f)), std::sqrt(match_dist_sq),
                                      std::sqrt(comp_dist_sq));
                            blocked_by_competition = true;
                            break;
                        }
                    }
                }

                if (!blocked_by_competition) {
                    // matches store (detection_idx, track_idx) for compatibility with track()
                    matches.push_back({det_idx_match, trk_idx_match});
                    det_matched[col] = true;
                }
            } else {
                // Log when a confirmed track fails cosine threshold
                int trk_idx = track_indices_l[row];
                int det_idx = unmatched_detections[col];
                // Recompute the actual min cosine distance (before gating) for diagnostics
                float actual_min_cos = 1.0f;
                for (const auto &tf : tracks_[trk_idx]->features()) {
                    float d = calculate_cosine_distance(detections[det_idx].feature, tf);
                    actual_min_cos = std::min(actual_min_cos, d);
                }
                auto gd = tracks_[trk_idx]->gating_distance(detections, {det_idx}, true);
                float maha = gd.empty() ? -1.f : gd[0];
                GST_DEBUG("CASCADE REJECT: track_id=%d(tsu=%d) vs det[%d] "
                          "cosine_dist=%.4f (threshold=%.3f), mahal_pos=%.2f (gate=%.2f), "
                          "num_features=%zu, level=%d",
                          tracks_[trk_idx]->track_id(), tracks_[trk_idx]->time_since_update(), det_idx, actual_min_cos,
                          max_cosine_distance_, maha, CHI2INV95_2DOF, tracks_[trk_idx]->features().size(), level);
            }
        }

        // Remaining unmatched detections carry forward to next cascade level
        std::vector<int> new_unmatched_detections;
        for (size_t col = 0; col < n_dets; ++col) {
            if (!det_matched[col]) {
                new_unmatched_detections.push_back(unmatched_detections[col]);
            }
        }
        unmatched_detections = new_unmatched_detections;
    }

    // Unmatched tracks = those not in any match
    std::set<int> matched_track_set;
    for (const auto &match : matches) {
        matched_track_set.insert(match.second);
    }
    for (int k : track_indices) {
        if (matched_track_set.find(k) == matched_track_set.end()) {
            unmatched_tracks.push_back(k);
        }
    }
}

/**
 * @brief IoU-based matching for stage 2 (unconfirmed + recently-missed tracks)
 */
void DeepSortTracker::min_cost_matching_iou(const std::vector<Detection> &detections,
                                            const std::vector<int> &track_indices,
                                            const std::vector<int> &detection_indices,
                                            std::vector<std::pair<int, int>> &matches,
                                            std::vector<int> &unmatched_tracks,
                                            std::vector<int> &unmatched_detections) {
    matches.clear();
    unmatched_tracks.clear();
    unmatched_detections.clear();

    if (track_indices.empty() || detection_indices.empty()) {
        unmatched_tracks.assign(track_indices.begin(), track_indices.end());
        unmatched_detections.assign(detection_indices.begin(), detection_indices.end());
        return;
    }

    size_t n_tracks = track_indices.size();
    size_t n_dets = detection_indices.size();

    // Build IoU cost matrix: cost = 1 - IoU
    std::vector<std::vector<float>> cost_matrix(n_tracks, std::vector<float>(n_dets));

    for (size_t row = 0; row < n_tracks; ++row) {
        int trk_idx = track_indices[row];

        // Tracks with tsu > 1 cannot match in IoU stage
        if (tracks_[trk_idx]->time_since_update() > 1) {
            GST_DEBUG("IOU STAGE2: track_id=%d BLOCKED by tsu=%d > 1", tracks_[trk_idx]->track_id(),
                      tracks_[trk_idx]->time_since_update());
            for (size_t col = 0; col < n_dets; ++col) {
                cost_matrix[row][col] = INFTY_COST;
            }
            continue;
        }

        cv::Rect_<float> track_bbox = tracks_[trk_idx]->to_bbox();
        for (size_t col = 0; col < n_dets; ++col) {
            int det_idx = detection_indices[col];
            float iou = calculate_iou(detections[det_idx].bbox, track_bbox);
            cost_matrix[row][col] = 1.0f - iou; // IoU distance
        }
    }

    // Gate by max_iou_distance: cost > threshold means too far apart
    for (size_t row = 0; row < n_tracks; ++row) {
        for (size_t col = 0; col < n_dets; ++col) {
            if (cost_matrix[row][col] > max_iou_distance_) {
                cost_matrix[row][col] = max_iou_distance_ + 1e-5f;
            }
        }
    }

    // Solve assignment with Hungarian algorithm
    std::vector<std::pair<int, int>> assignments;
    hungarian_assignment(cost_matrix, assignments);

    // Process assignments
    std::vector<bool> trk_matched(n_tracks, false);
    std::vector<bool> det_matched(n_dets, false);

    for (const auto &[row, col] : assignments) {
        if (cost_matrix[row][col] <= max_iou_distance_) {
            matches.push_back({detection_indices[col], track_indices[row]});
            trk_matched[row] = true;
            det_matched[col] = true;
        } else {
            int trk_idx = track_indices[row];
            int det_idx = detection_indices[col];
            float actual_iou = calculate_iou(detections[det_idx].bbox, tracks_[trk_idx]->to_bbox());
            GST_DEBUG("IOU REJECT: track_id=%d vs det[%d] iou=%.3f cost=%.3f (threshold=%.3f)",
                      tracks_[trk_idx]->track_id(), det_idx, actual_iou, cost_matrix[row][col], max_iou_distance_);
        }
    }

    for (size_t row = 0; row < n_tracks; ++row) {
        if (!trk_matched[row]) {
            unmatched_tracks.push_back(track_indices[row]);
        }
    }
    for (size_t col = 0; col < n_dets; ++col) {
        if (!det_matched[col]) {
            unmatched_detections.push_back(detection_indices[col]);
        }
    }
}

/**
 * @brief Gate cost matrix using Mahalanobis distance and TSU-scaled spatial gating
 */
void DeepSortTracker::gate_cost_matrix(std::vector<std::vector<float>> &cost_matrix,
                                       const std::vector<Detection> &detections, const std::vector<int> &track_indices,
                                       const std::vector<int> &detection_indices) {
    // Combined Mahalanobis + TSU-scaled spatial gating.
    // A match is blocked (INFTY_COST) if EITHER gate rejects it:
    //
    // 1) Mahalanobis gate (from original Deep SORT): uses Kalman filter's predicted
    //    covariance to reject geometrically implausible matches. Effective when the
    //    track was recently seen (tsu=1) and Kalman state is well-calibrated.
    //
    // 2) TSU-scaled spatial gate: normalized center distance must be < base_gate/sqrt(tsu).
    //    Tightens for stale tracks to prevent them from "stealing" detections that
    //    belong to other tracks via cascade priority. Effective when Kalman covariance
    //    has grown large (high tsu) and Mahalanobis alone is too permissive.
    //
    // 3) Soft spatial penalty: tie-breaking for spatially closer track when cosine
    //    distances are nearly identical.
    constexpr float base_gate = 1.0f; // Base spatial gate at tsu=1: 1.0x min(track,det) height
    constexpr float min_gate = 0.4f;  // Minimum spatial gate for very stale tracks
    constexpr float spatial_weight = 0.1f;
    constexpr float max_spatial_penalty = 0.15f;

    for (size_t row = 0; row < track_indices.size(); ++row) {
        int trk_idx = track_indices[row];

        // Mahalanobis gating: compute squared Mahalanobis distances for all detections
        auto maha_dists = tracks_[trk_idx]->gating_distance(detections, detection_indices, true);

        cv::Rect_<float> track_bbox = tracks_[trk_idx]->to_bbox();
        float trk_cx = track_bbox.x + track_bbox.width / 2.0f;
        float trk_cy = track_bbox.y + track_bbox.height / 2.0f;
        float height = std::max(track_bbox.height, 1.0f);

        // TSU-scaled spatial gate
        int tsu = tracks_[trk_idx]->time_since_update();
        float max_dist_factor = std::max(base_gate / std::sqrt(static_cast<float>(tsu)), min_gate);

        for (size_t col = 0; col < detection_indices.size(); ++col) {
            if (cost_matrix[row][col] >= INFTY_COST)
                continue;

            // Gate 1: Mahalanobis (position-only, chi2 95% with 2 DOF)
            if (maha_dists[col] > CHI2INV95_2DOF) {
                cost_matrix[row][col] = INFTY_COST;
                continue;
            }

            // Gate 2: TSU-scaled spatial distance
            int det_idx = detection_indices[col];
            const cv::Rect_<float> &det_bbox = detections[det_idx].bbox;
            float det_cx = det_bbox.x + det_bbox.width / 2.0f;
            float det_cy = det_bbox.y + det_bbox.height / 2.0f;
            float dx = trk_cx - det_cx;
            float dy = trk_cy - det_cy;
            float min_height = std::min(height, std::max(det_bbox.height, 1.0f));
            float norm_dist = std::sqrt(dx * dx + dy * dy) / min_height;

            if (norm_dist > max_dist_factor) {
                cost_matrix[row][col] = INFTY_COST;
            } else {
                cost_matrix[row][col] += std::min(spatial_weight * norm_dist, max_spatial_penalty);
            }
        }
    }
}

/**
 * @brief Calculate cosine distance between two feature vectors (0=identical, 1=opposite)
 */
float DeepSortTracker::calculate_cosine_distance(const std::vector<float> &feat1, const std::vector<float> &feat2) {
    if (feat1.size() != feat2.size()) {
        return 1.0f; // Maximum distance for mismatched features
    }

    // Re-normalize to unit length in double precision, then compute 1.0 - dot_product
    double norm1_sq = 0.0, norm2_sq = 0.0, dot = 0.0;
    for (size_t i = 0; i < feat1.size(); ++i) {
        double a = static_cast<double>(feat1[i]);
        double b = static_cast<double>(feat2[i]);
        norm1_sq += a * a;
        norm2_sq += b * b;
        dot += a * b;
    }
    double norm1 = std::sqrt(norm1_sq);
    double norm2 = std::sqrt(norm2_sq);
    if (norm1 > 0.0 && norm2 > 0.0) {
        dot /= (norm1 * norm2);
    }
    return static_cast<float>(1.0 - dot);
}

/**
 * @brief Calculate Intersection over Union (IoU) between two bounding boxes (0=no overlap, 1=perfect match)
 */
float DeepSortTracker::calculate_iou(const cv::Rect_<float> &bbox1, const cv::Rect_<float> &bbox2) {
    cv::Rect_<float> intersection_rect = bbox1 & bbox2;
    float intersection_area = intersection_rect.area();
    float union_area = bbox1.area() + bbox2.area() - intersection_area;

    float iou = union_area > 0.0f ? intersection_area / union_area : 0.0f;
    return iou;
}

/**
 * @brief Full Hungarian (Kuhn-Munkres) algorithm for optimal assignment
 */
void DeepSortTracker::hungarian_assignment(const std::vector<std::vector<float>> &cost_matrix,
                                           std::vector<std::pair<int, int>> &assignments) {
    assignments.clear();

    if (cost_matrix.empty())
        return;

    // Epsilon tolerance for floating-point zero comparison
    constexpr float ZERO_THRESH = 1e-6f;

    size_t orig_rows = cost_matrix.size();
    size_t orig_cols = cost_matrix[0].size();

    // Pad to square matrix with zeros for dummy rows/columns
    size_t n = std::max(orig_rows, orig_cols);
    std::vector<std::vector<float>> matrix(n, std::vector<float>(n, 0.0f));
    for (size_t i = 0; i < orig_rows; ++i) {
        for (size_t j = 0; j < orig_cols; ++j) {
            matrix[i][j] = cost_matrix[i][j];
        }
    }

    size_t rows = n;
    size_t cols = n;

    // Step 1: Subtract row minimums
    for (size_t i = 0; i < rows; ++i) {
        float row_min = *std::min_element(matrix[i].begin(), matrix[i].end());
        for (size_t j = 0; j < cols; ++j) {
            matrix[i][j] -= row_min;
        }
    }

    // Step 2: Subtract column minimums
    for (size_t j = 0; j < cols; ++j) {
        float col_min = matrix[0][j];
        for (size_t i = 1; i < rows; ++i) {
            col_min = std::min(col_min, matrix[i][j]);
        }
        for (size_t i = 0; i < rows; ++i) {
            matrix[i][j] -= col_min;
        }
    }

    // Track assignments and coverage
    std::vector<std::vector<int>> marks(rows, std::vector<int>(cols, 0)); // 0=none, 1=star, 2=prime
    std::vector<bool> row_covered(rows, false);
    std::vector<bool> col_covered(cols, false);

    // Step 3: Cover all zeros with minimum number of lines
    // First, find a zero and star it if no other star in same row/column
    for (size_t i = 0; i < rows; ++i) {
        for (size_t j = 0; j < cols; ++j) {
            if (matrix[i][j] <= ZERO_THRESH && !row_covered[i] && !col_covered[j]) {
                marks[i][j] = 1; // Star this zero
                row_covered[i] = true;
                col_covered[j] = true;
            }
        }
    }

    // Reset coverage for next steps
    std::fill(row_covered.begin(), row_covered.end(), false);
    std::fill(col_covered.begin(), col_covered.end(), false);

    // Cover all columns with starred zeros
    for (size_t i = 0; i < rows; ++i) {
        for (size_t j = 0; j < cols; ++j) {
            if (marks[i][j] == 1) {
                col_covered[j] = true;
            }
        }
    }

    // Main Hungarian algorithm loop
    bool done = false;
    while (!done) {
        // Check if we have enough lines to cover all zeros
        size_t covered_cols = 0;
        for (size_t j = 0; j < cols; ++j) {
            if (col_covered[j])
                covered_cols++;
        }

        if (covered_cols >= std::min(rows, cols)) {
            done = true;
        } else {
            // Find an uncovered zero and prime it
            bool found_uncovered_zero = false;
            size_t zero_row = 0, zero_col = 0;

            for (size_t i = 0; i < rows && !found_uncovered_zero; ++i) {
                for (size_t j = 0; j < cols && !found_uncovered_zero; ++j) {
                    if (matrix[i][j] <= ZERO_THRESH && !row_covered[i] && !col_covered[j]) {
                        zero_row = i;
                        zero_col = j;
                        found_uncovered_zero = true;
                        marks[i][j] = 2; // Prime this zero
                    }
                }
            }

            if (found_uncovered_zero) {
                // Check if there's a starred zero in the same row
                bool star_in_row = false;
                size_t star_col = 0;
                for (size_t j = 0; j < cols; ++j) {
                    if (marks[zero_row][j] == 1) {
                        star_in_row = true;
                        star_col = j;
                        break;
                    }
                }

                if (star_in_row) {
                    // Cover this row and uncover the starred column
                    row_covered[zero_row] = true;
                    col_covered[star_col] = false;
                } else {
                    // Construct alternating path and adjust stars
                    std::vector<std::pair<size_t, size_t>> path;
                    path.push_back({zero_row, zero_col});

                    bool path_done = false;
                    while (!path_done) {
                        // Find starred zero in primed zero's column
                        bool found_star = false;
                        size_t star_row = 0;
                        for (size_t i = 0; i < rows; ++i) {
                            if (marks[i][path.back().second] == 1) {
                                star_row = i;
                                found_star = true;
                                break;
                            }
                        }

                        if (found_star) {
                            path.push_back({star_row, path.back().second});

                            // Find primed zero in starred zero's row
                            for (size_t j = 0; j < cols; ++j) {
                                if (marks[star_row][j] == 2) {
                                    path.push_back({star_row, j});
                                    break;
                                }
                            }
                        } else {
                            path_done = true;
                        }
                    }

                    // Unstar each starred zero and star each primed zero in path
                    for (size_t p = 0; p < path.size(); ++p) {
                        if (p % 2 == 0) {
                            marks[path[p].first][path[p].second] = 1; // Star
                        } else {
                            marks[path[p].first][path[p].second] = 0; // Unstar
                        }
                    }

                    // Clear all primes and reset coverage
                    for (size_t i = 0; i < rows; ++i) {
                        for (size_t j = 0; j < cols; ++j) {
                            if (marks[i][j] == 2)
                                marks[i][j] = 0;
                        }
                    }
                    std::fill(row_covered.begin(), row_covered.end(), false);
                    std::fill(col_covered.begin(), col_covered.end(), false);

                    // Cover columns with starred zeros
                    for (size_t i = 0; i < rows; ++i) {
                        for (size_t j = 0; j < cols; ++j) {
                            if (marks[i][j] == 1) {
                                col_covered[j] = true;
                            }
                        }
                    }
                }
            } else {
                // No uncovered zeros - add minimum uncovered value
                float min_uncovered = std::numeric_limits<float>::max();
                for (size_t i = 0; i < rows; ++i) {
                    for (size_t j = 0; j < cols; ++j) {
                        if (!row_covered[i] && !col_covered[j]) {
                            min_uncovered = std::min(min_uncovered, matrix[i][j]);
                        }
                    }
                }

                // Subtract from uncovered, add to doubly covered
                for (size_t i = 0; i < rows; ++i) {
                    for (size_t j = 0; j < cols; ++j) {
                        if (row_covered[i] && col_covered[j]) {
                            matrix[i][j] += min_uncovered;
                        } else if (!row_covered[i] && !col_covered[j]) {
                            matrix[i][j] -= min_uncovered;
                        }
                    }
                }
            }
        }
    }

    // Extract final assignments from starred positions, filtering out dummy rows/columns
    for (size_t i = 0; i < orig_rows; ++i) {
        for (size_t j = 0; j < orig_cols; ++j) {
            if (marks[i][j] == 1) {
                assignments.emplace_back(i, j);
            }
        }
    }
}

/**
 * @brief Parse Deep SORT tracking configuration string
 */
void DeepSortTracker::parse_dps_trck_config() {
    // Parse tracking configuration
    auto cfg = Utils::stringToMap(dptrckcfg_);
    auto iter = cfg.end();
    try {
        iter = cfg.find("max_iou_distance");
        if (iter != cfg.end()) {
            max_iou_distance_ = std::stof(iter->second);
            cfg.erase(iter);
        }
        iter = cfg.find("max_age");
        if (iter != cfg.end()) {
            max_age_ = std::stoi(iter->second);
            cfg.erase(iter);
        }
        iter = cfg.find("n_init");
        if (iter != cfg.end()) {
            n_init_ = std::stoi(iter->second);
            cfg.erase(iter);
        }
        iter = cfg.find("max_cosine_distance");
        if (iter != cfg.end()) {
            max_cosine_distance_ = std::stof(iter->second);
            cfg.erase(iter);
        }
        iter = cfg.find("nn_budget");
        if (iter != cfg.end()) {
            nn_budget_ = std::stoi(iter->second);
            cfg.erase(iter);
        }
        iter = cfg.find("object_class");
        if (iter != cfg.end()) {
            object_class_ = iter->second;
            cfg.erase(iter);
        }
        iter = cfg.find("reid_max_age");
        if (iter != cfg.end()) {
            reid_max_age_ = std::stoi(iter->second);
            cfg.erase(iter);
        }
    } catch (...) {
        if (iter == cfg.end())
            std::throw_with_nested(
                std::runtime_error("[DeepSortTracker] Error occured while parsing key/value parameters"));
        std::throw_with_nested(
            std::runtime_error("[DeepSortTracker] Invalid value provided for parameter: " + iter->first));
    }
}

} // namespace DeepSortWrapper