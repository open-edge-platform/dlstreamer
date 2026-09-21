/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "calibration.h"

#include <cstdint>
#include <opencv2/core.hpp>
#include <unordered_map>
#include <vector>

namespace dlstreamer {
constexpr unsigned kInvalidMtdId = 0xFFFFFFFFu;

/** A 2D detection in image-space (camera or projected from 3D sensor). */
struct Box2D {
    cv::Rect2f rect;
    int class_label = -1;
    float confidence = 0.f;
    int64_t track_id = -1;           /* per-modality track id */
    int detection_index = -1;        /* original index in source vector */
    unsigned mtd_id = kInvalidMtdId; /* GstAnalyticsODMtd id on the source buffer */
};

/** A 3D oriented bounding box (lidar or radar). */
struct Box3D {
    float x = 0, y = 0, z = 0;
    float length = 0, width = 0, height = 0;
    float yaw = 0;
    int class_label = -1;
    float confidence = 0.f;
    int64_t track_id = -1;
    int detection_index = -1;
    unsigned mtd_id = kInvalidMtdId; /* existing GstAnalytics3DODMtd id */
};

/** Result of fusing one camera frame with one 3D-sensor frame. */
struct FusionResult {
    /* For each camera detection (input order), the index into @c box3d_input
     * vector that it was associated with (or -1). */
    std::vector<int> camera_to_3d;
    /* Inverse mapping: index of camera box for each 3D box, or -1. */
    std::vector<int> threed_to_camera;
    /* Stable cross-modal id assigned by the track-to-track Hungarian. One per
     * fused pair. Pairs that are unmatched have fused_id == -1. */
    std::vector<int64_t> camera_fused_ids;
    std::vector<int64_t> threed_fused_ids;
};

class ObjectFuser {
  public:
    ObjectFuser();

    void set_iou_threshold(float t) {
        iou_threshold_ = t;
    }
    void set_history_window(unsigned w) {
        history_window_ = w;
    }
    void set_calibration_store(const CalibrationStore *store) {
        calibration_ = store;
    }

    /** Project a 3D box into the image plane using KITTI-style calibration.
     *  Returns the axis-aligned bounding rectangle of the projected 8 corners.
     *  Returns false if projection cannot be performed for this calibration.  */
    static bool project_lidar_box_to_image(const Box3D &box, const CameraCalibration &cal, cv::Rect2f &out_rect);

    /** Rasterise a 3D box's ground footprint (x, y) to the fixed top-down BEV
     *  pixel grid. Camera-independent (needs no calibration) and always succeeds,
     *  so LiDAR boxes are tracked in a metric frame free of perspective/depth
     *  ambiguity. See @ref bev_raster_size for the grid extent. */
    static bool project_lidar_box_to_bev(const Box3D &box, cv::Rect2f &out_rect);

    /** Pixel dimensions of the BEV grid used by @ref project_lidar_box_to_bev.
     *  Size the tracker's frame to this so in-range boxes are not pruned as
     *  out-of-frame. */
    static cv::Size bev_raster_size();

    /** Project a radar (x, y) ground point + extents to the image plane via the 3x3 homography. */
    static bool project_radar_box_to_image(const Box3D &box, const CameraCalibration &cal, cv::Rect2f &out_rect);

    /** IoU between two axis-aligned 2D rects. */
    static float iou(const cv::Rect2f &a, const cv::Rect2f &b);

    /** Run the full fusion + track-to-track for one frame. Returns associations. */
    FusionResult fuse(int camera_index, const std::vector<Box2D> &cam_input, const std::vector<Box3D> &box3d_input,
                      bool is_lidar);

  private:
    struct PairKey {
        int camera_index;
        int64_t cam_track;
        int64_t threed_track;
        bool operator==(const PairKey &o) const {
            return camera_index == o.camera_index && cam_track == o.cam_track && threed_track == o.threed_track;
        }
    };
    struct PairKeyHash {
        std::size_t operator()(const PairKey &k) const noexcept {
            return std::hash<int>()(k.camera_index) ^ (std::hash<int64_t>()(k.cam_track) << 1) ^
                   (std::hash<int64_t>()(k.threed_track) << 2);
        }
    };

    float iou_threshold_ = 0.3f;
    unsigned history_window_ = 30;
    const CalibrationStore *calibration_ = nullptr;
    int64_t next_fused_id_ = 1;

    /* Persistent (camera_track, 3d_track) -> fused_id table. */
    std::unordered_map<PairKey, int64_t, PairKeyHash> pair_to_fused_id_;
    /* Per-pair age counter for pruning stale entries. */
    std::unordered_map<PairKey, unsigned, PairKeyHash> pair_age_;
};

} // namespace dlstreamer
