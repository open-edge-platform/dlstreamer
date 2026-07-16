/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace dlstreamer {

/**
 * Calibration parameters for a single camera.
 *
 * For lidar-mode pipelines (KITTI-style):
 *   tr_velo_to_cam : 4x4 homogeneous transform from lidar to (unrectified) camera frame
 *   r0_rect        : 4x4 camera rectification matrix
 *   p2             : 3x4 camera projection (intrinsics including baseline)
 *
 * For radar-mode pipelines:
 *   homography     : 3x3 matrix mapping radar (x, y) ground points to image pixels
 */
struct CameraCalibration {
    bool has_lidar_calib = false;
    bool has_radar_calib = false;

    std::array<float, 16> tr_velo_to_cam{};
    std::array<float, 16> r0_rect{};
    std::array<float, 12> p2{};

    std::array<float, 9> homography{};
};

/**
 * Multi-camera calibration container; cameras keyed by stream index emitted by
 * gvastreammux. For single-camera pipelines, key 0 is used.
 */
class CalibrationStore {
  public:
    /** Load a calibration JSON file. Returns true on success. */
    bool load(const std::string &path);

    /** Get calibration for the given stream/camera index, or nullptr. */
    const CameraCalibration *get(int camera_index) const;

    /** All camera indices present in the store, in ascending order. */
    std::vector<int> camera_indices() const;

    /** Last error message from the most recent load() call. */
    const std::string &last_error() const {
        return last_error_;
    }

  private:
    std::map<int, CameraCalibration> per_camera_;
    std::string last_error_;
};

} // namespace dlstreamer
