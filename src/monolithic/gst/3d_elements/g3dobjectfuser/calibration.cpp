/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "calibration.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace dlstreamer {

namespace {

template <std::size_t N>
bool read_flat_array(const nlohmann::json &j, const char *key, std::array<float, N> &out) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != N)
        return false;
    for (std::size_t i = 0; i < N; ++i)
        out[i] = it->at(i).get<float>();
    return true;
}

bool parse_camera(const nlohmann::json &j, CameraCalibration &cal) {
    bool got_lidar = read_flat_array<16>(j, "tr_velo_to_cam", cal.tr_velo_to_cam) &&
                     read_flat_array<16>(j, "r0_rect", cal.r0_rect) && read_flat_array<12>(j, "p2", cal.p2);
    bool got_radar = read_flat_array<9>(j, "homography", cal.homography);

    cal.has_lidar_calib = got_lidar;
    cal.has_radar_calib = got_radar;
    return got_lidar || got_radar;
}

} // namespace

bool CalibrationStore::load(const std::string &path) {
    last_error_.clear();
    per_camera_.clear();

    std::ifstream in(path);
    if (!in.is_open()) {
        last_error_ = "Failed to open calibration file: " + path;
        return false;
    }

    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const std::exception &e) {
        last_error_ = std::string("JSON parse error: ") + e.what();
        return false;
    }

    if (doc.contains("cameras") && doc["cameras"].is_object()) {
        for (auto it = doc["cameras"].begin(); it != doc["cameras"].end(); ++it) {
            int idx = 0;
            try {
                idx = std::stoi(it.key());
            } catch (...) {
                last_error_ = "Camera key must be an integer index: " + it.key();
                return false;
            }
            CameraCalibration cal;
            if (!parse_camera(it.value(), cal)) {
                std::ostringstream oss;
                oss << "Camera " << idx << " calibration missing required fields";
                last_error_ = oss.str();
                return false;
            }
            per_camera_[idx] = cal;
        }
    } else {
        CameraCalibration cal;
        if (!parse_camera(doc, cal)) {
            last_error_ = "Calibration file missing required matrices (tr_velo_to_cam/r0_rect/p2 or homography)";
            return false;
        }
        per_camera_[0] = cal;
    }

    return !per_camera_.empty();
}

const CameraCalibration *CalibrationStore::get(int camera_index) const {
    auto it = per_camera_.find(camera_index);
    if (it != per_camera_.end())
        return &it->second;
    /* Single-camera fallback */
    auto fb = per_camera_.find(0);
    return fb != per_camera_.end() ? &fb->second : nullptr;
}

std::vector<int> CalibrationStore::camera_indices() const {
    std::vector<int> indices;
    indices.reserve(per_camera_.size());
    for (const auto &kv : per_camera_) /* std::map keeps keys ascending */
        indices.push_back(kv.first);
    return indices;
}

} // namespace dlstreamer
