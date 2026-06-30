/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "object_fuser_impl.h"

#include "vas/components/ot/mtt/hungarian_wrap.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dlstreamer {

namespace {

/* Build the 8 corners of a 3D oriented bounding box (yaw around Z), in the
 * lidar/world frame. Returns a 3x8 cv::Mat of float32. */
cv::Mat make_3d_box_corners(const Box3D &b) {
    /* Uses the convention: w == box.length, l == box.width so projected
     * boxes line up with KITTI examples. */
    float l = b.width;
    float w = b.length;
    float h = b.height;
    float corners_local[8][3] = {{-l / 2, -w / 2, -h / 2}, {l / 2, -w / 2, -h / 2}, {l / 2, w / 2, -h / 2},
                                 {-l / 2, w / 2, -h / 2},  {-l / 2, -w / 2, h / 2}, {l / 2, -w / 2, h / 2},
                                 {l / 2, w / 2, h / 2},    {-l / 2, w / 2, h / 2}};

    float cy = std::cos(b.yaw), sy = std::sin(b.yaw);
    cv::Mat out(3, 8, CV_32FC1);
    for (int i = 0; i < 8; ++i) {
        float x = corners_local[i][0];
        float y = corners_local[i][1];
        float z = corners_local[i][2];
        float xr = cy * x - sy * y;
        float yr = sy * x + cy * y;
        out.at<float>(0, i) = xr + b.x;
        out.at<float>(1, i) = yr + b.y;
        out.at<float>(2, i) = z + b.z;
    }
    return out;
}

cv::Mat to_mat4x4(const std::array<float, 16> &arr) {
    cv::Mat m(4, 4, CV_32FC1);
    for (int i = 0; i < 16; ++i)
        m.at<float>(i / 4, i % 4) = arr[i];
    return m;
}

cv::Mat to_mat3x4(const std::array<float, 12> &arr) {
    cv::Mat m(3, 4, CV_32FC1);
    for (int i = 0; i < 12; ++i)
        m.at<float>(i / 4, i % 4) = arr[i];
    return m;
}

cv::Mat to_mat3x3(const std::array<float, 9> &arr) {
    cv::Mat m(3, 3, CV_32FC1);
    for (int i = 0; i < 9; ++i)
        m.at<float>(i / 3, i % 3) = arr[i];
    return m;
}

} // namespace

ObjectFuser::ObjectFuser() = default;

bool ObjectFuser::project_lidar_box_to_image(const Box3D &box, const CameraCalibration &cal, cv::Rect2f &out_rect) {
    if (!cal.has_lidar_calib)
        return false;

    cv::Mat corners3d = make_3d_box_corners(box); // 3x8
    cv::Mat homog = cv::Mat::ones(4, 8, CV_32FC1);
    corners3d.copyTo(homog(cv::Rect(0, 0, 8, 3)));

    cv::Mat Tr = to_mat4x4(cal.tr_velo_to_cam);
    cv::Mat R0 = to_mat4x4(cal.r0_rect);
    cv::Mat P2 = to_mat3x4(cal.p2);

    cv::Mat in_cam = Tr * homog;             // 4x8
    cv::Mat rectified = R0 * in_cam;         // 4x8
    cv::Mat in_image = (P2 * rectified).t(); // 8x3

    float xmin = std::numeric_limits<float>::infinity();
    float ymin = std::numeric_limits<float>::infinity();
    float xmax = -std::numeric_limits<float>::infinity();
    float ymax = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < in_image.rows; ++i) {
        float x = in_image.at<float>(i, 0);
        float y = in_image.at<float>(i, 1);
        float z = in_image.at<float>(i, 2);
        if (z <= 0.f)
            continue; /* corner is behind the image plane */
        float u = x / z;
        float v = y / z;
        xmin = std::min(xmin, u);
        ymin = std::min(ymin, v);
        xmax = std::max(xmax, u);
        ymax = std::max(ymax, v);
    }

    if (!std::isfinite(xmin) || !std::isfinite(xmax))
        return false;
    out_rect = cv::Rect2f(xmin, ymin, std::max(0.f, xmax - xmin), std::max(0.f, ymax - ymin));
    return true;
}

bool ObjectFuser::project_radar_box_to_image(const Box3D &box, const CameraCalibration &cal, cv::Rect2f &out_rect) {
    if (!cal.has_radar_calib)
        return false;

    cv::Mat H = to_mat3x3(cal.homography);
    /* Radar point (x, y) on the ground plane -> pixel via H. The 3D box has no
     * explicit footprint; we approximate with a box of width/length in metres. */
    auto project_point = [&](float x, float y, cv::Point2f &p) -> bool {
        cv::Mat src = (cv::Mat_<float>(3, 1) << x, y, 1.f);
        cv::Mat dst = H * src;
        float w = dst.at<float>(2, 0);
        if (std::fabs(w) < 1e-6f)
            return false;
        p = cv::Point2f(dst.at<float>(0, 0) / w, dst.at<float>(1, 0) / w);
        return true;
    };

    float hl = box.length * 0.5f;
    float hw = box.width * 0.5f;
    cv::Point2f corners[4];
    if (!project_point(box.x - hl, box.y - hw, corners[0]))
        return false;
    if (!project_point(box.x + hl, box.y - hw, corners[1]))
        return false;
    if (!project_point(box.x + hl, box.y + hw, corners[2]))
        return false;
    if (!project_point(box.x - hl, box.y + hw, corners[3]))
        return false;

    float xmin = corners[0].x, xmax = corners[0].x;
    float ymin = corners[0].y, ymax = corners[0].y;
    for (int i = 1; i < 4; ++i) {
        xmin = std::min(xmin, corners[i].x);
        xmax = std::max(xmax, corners[i].x);
        ymin = std::min(ymin, corners[i].y);
        ymax = std::max(ymax, corners[i].y);
    }
    out_rect = cv::Rect2f(xmin, ymin, std::max(0.f, xmax - xmin), std::max(0.f, ymax - ymin));
    return true;
}

float ObjectFuser::iou(const cv::Rect2f &a, const cv::Rect2f &b) {
    float x1 = std::max(a.x, b.x);
    float y1 = std::max(a.y, b.y);
    float x2 = std::min(a.x + a.width, b.x + b.width);
    float y2 = std::min(a.y + a.height, b.y + b.height);
    float iw = std::max(0.f, x2 - x1);
    float ih = std::max(0.f, y2 - y1);
    float inter = iw * ih;
    float uni = a.width * a.height + b.width * b.height - inter;
    return uni > 0.f ? inter / uni : 0.f;
}

FusionResult ObjectFuser::fuse(int camera_index, const std::vector<Box2D> &cam_input,
                               const std::vector<Box3D> &box3d_input, bool is_lidar) {
    FusionResult result;
    result.camera_to_3d.assign(cam_input.size(), -1);
    result.threed_to_camera.assign(box3d_input.size(), -1);
    result.camera_fused_ids.assign(cam_input.size(), -1);
    result.threed_fused_ids.assign(box3d_input.size(), -1);

    if (cam_input.empty() || box3d_input.empty() || calibration_ == nullptr)
        return result;

    const CameraCalibration *cal = calibration_->get(camera_index);
    if (!cal)
        return result;

    /* Project each 3D box to image space. */
    std::vector<cv::Rect2f> projected(box3d_input.size());
    std::vector<bool> projected_ok(box3d_input.size(), false);
    for (std::size_t i = 0; i < box3d_input.size(); ++i) {
        bool ok = is_lidar ? project_lidar_box_to_image(box3d_input[i], *cal, projected[i])
                           : project_radar_box_to_image(box3d_input[i], *cal, projected[i]);
        projected_ok[i] = ok;
    }

    /* Build cost matrix: rows = camera detections, cols = 3D detections.
     * Cost = 1 - IoU; values >= 1 are treated as forbidden. */
    const int R = static_cast<int>(cam_input.size());
    const int C = static_cast<int>(box3d_input.size());
    constexpr float kInvalidCost = 2.0f;
    cv::Mat_<float> cost(R, C, kInvalidCost);
    for (int r = 0; r < R; ++r) {
        for (int c = 0; c < C; ++c) {
            if (!projected_ok[c])
                continue;
            float iou_v = iou(cam_input[r].rect, projected[c]);
            if (iou_v >= iou_threshold_)
                cost(r, c) = 1.0f - iou_v;
        }
    }

    /* Solve via Hungarian. The solver pads to a square matrix internally,
     * so we look up assignments only for the original R rows / C columns and
     * discard pairs above the invalid threshold. */
    vas::ot::HungarianAlgo hungarian(cost);
    cv::Mat_<uint8_t> assign = hungarian.Solve();
    for (int r = 0; r < R; ++r) {
        int c = -1;
        for (int j = 0; j < C; ++j) {
            if (assign(r, j)) {
                c = j;
                break;
            }
        }
        if (c < 0 || cost(r, c) >= 1.0f)
            continue;
        result.camera_to_3d[r] = c;
        result.threed_to_camera[c] = r;

        /* Track-to-track stable id table, scoped per camera so the same
         * camera-track-id from two cameras maps to two distinct fused ids. */
        PairKey k{camera_index, cam_input[r].track_id, box3d_input[c].track_id};
        int64_t fused_id;
        auto it = pair_to_fused_id_.find(k);
        if (it == pair_to_fused_id_.end()) {
            fused_id = next_fused_id_++;
            pair_to_fused_id_[k] = fused_id;
        } else {
            fused_id = it->second;
        }
        pair_age_[k] = 0;
        result.camera_fused_ids[r] = fused_id;
        result.threed_fused_ids[c] = fused_id;
    }

    /* Age all entries; prune ones older than history_window_. */
    for (auto it = pair_age_.begin(); it != pair_age_.end();) {
        ++it->second;
        if (it->second > history_window_) {
            pair_to_fused_id_.erase(it->first);
            it = pair_age_.erase(it);
        } else {
            ++it;
        }
    }

    return result;
}

} // namespace dlstreamer
