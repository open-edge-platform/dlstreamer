/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace g3d_bevfusion {

// Fixed by the exported BEVFusion IRs (openvino_contrib/modules/openvino_bevfusion).
// The runtime checks the model's config.json against these at load time.
constexpr int kNumCameras = 6;
constexpr int kImageH = 256;
constexpr int kImageW = 704;
constexpr int kFeatH = 32;
constexpr int kFeatW = 88;
constexpr int kDepthBins = 118;
constexpr int kContextChannels = 80;
constexpr int kNumClasses = 10;
constexpr int kNumProposals = 200;
constexpr int kPointDims = 5; // x, y, z, intensity, time lag
constexpr int kVoxelColumns = 9; // 5 mean features + batch, x, y, z
constexpr int kLidarBevC = 256;
constexpr int kLidarBevH = 180;
constexpr int kLidarBevW = 180;
constexpr int kCameraBevH = 360;
constexpr int kCameraBevW = 360;

using Mat3 = std::array<float, 9>;  // row-major
using Mat4 = std::array<float, 16>; // row-major

struct ModelParams {
    double resize_lim = 0.48;
    std::array<float, 3> mean{{0.485f, 0.456f, 0.406f}}; // RGB
    std::array<float, 3> std{{0.229f, 0.224f, 0.225f}};
    std::array<double, 3> dbound{{1.0, 60.0, 0.5}};
    std::array<double, 6> point_cloud_range{{-54.0, -54.0, -5.0, 54.0, 54.0, 3.0}};
    std::array<double, 3> voxel_size{{0.075, 0.075, 0.2}};
    std::array<double, 6> post_center_range{{-61.2, -61.2, -10.0, 61.2, 61.2, 10.0}};
    int out_size_factor = 8;
    std::vector<std::string> class_names;
};

// One camera of the sensor rig, in the model's slot order.
struct CameraCalibration {
    std::string name;
    Mat4 camera2lidar{};
    Mat3 intrinsics{};
};

// One lidar turn of a multi-sweep point cloud: the next `points` points came
// from a turn `time_lag` seconds before the frame's lidar timestamp.
struct LidarSweep {
    size_t points = 0;
    float time_lag = 0.0f;
};

// A packed BGR8 image as it arrives from the pipeline.
struct ImageView {
    const uint8_t *data = nullptr;
    int width = 0;
    int height = 0;
    size_t stride = 0; // bytes per row
};

// Decoded detection, in the model's convention: lidar frame, z at the box
// bottom, dx along the heading (yaw, counter-clockwise from +x).
struct Box3D {
    float x, y, z_bottom;
    float dx, dy, dz;
    float yaw;
    float vx, vy;
    float score;
    int label;
};

// Raw outputs of full_detection, in its output order.
struct HeadOutputs {
    std::vector<float> center;                // [1, 2, P]
    std::vector<float> height;                // [1, 1, P]
    std::vector<float> dim;                   // [1, 3, P]
    std::vector<float> rot;                   // [1, 2, P]
    std::vector<float> vel;                   // [1, 2, P]
    std::vector<float> heatmap;               // [1, K, P]
    std::vector<float> query_heatmap_score;   // [1, K, P]
    std::vector<int64_t> top_proposals_class; // [1, P]
};

// Resize by resize_lim, crop the bottom-centre 704x256, BGR -> RGB, normalise,
// and write CHW floats to `out` (3 * kImageH * kImageW). Returns the matching
// img_aug_matrix (scale + crop translation).
Mat4 preprocess_image(const ImageView &image, const ModelParams &params, float *out);

// lidar2image = [K 0; 0 1] * inverse(camera2lidar)
Mat4 lidar2image(const CameraCalibration &camera);

// Sparse per-camera depth image from the lidar points, as the camera
// branch's depth input: [kNumCameras, 1, kImageH, kImageW].
void depth_maps(const float *points, size_t num_points, size_t point_stride, const std::array<Mat4, kNumCameras> &l2i,
                const std::array<Mat4, kNumCameras> &aug, const ModelParams &params, float *out);

// Inputs of the geometry IR: combined_matrices [6, 14] and camera2lidar_rot [6, 3, 3].
void pack_geometry(const std::array<Mat4, kNumCameras> &aug, const std::array<CameraCalibration, kNumCameras> &cams,
                   float *combined, float *rotation);

// Copy (x, y, z, intensity) points into the voxelize input [max_points, 5].
// The 5th column is the time lag of the turn each point came from, with
// `sweeps` listing the turns in point order; points past the listed turns, or
// all of them without sweeps, get 0 (the current turn). Points beyond
// max_points are dropped. Rows from the returned count up to `stale_rows`
// (what an earlier call left there) are zeroed.
// Returns the number of points used.
int pack_points(const float *points, size_t num_points, size_t point_stride, const std::vector<LidarSweep> &sweeps,
                int max_points, float *out, int stale_rows);

// Turn the voxelize result [max_voxels + 1, 9] into the sparse encoder inputs
// [max_voxels, 5] and [max_voxels, 4]. Rows from the returned count up to
// `stale_rows` are zeroed. Returns the number of voxels.
int unpack_voxels(const float *result, int max_voxels, float *features, int32_t *coords, int stale_rows);

std::vector<Box3D> decode(const HeadOutputs &head, const ModelParams &params, float score_threshold);

} // namespace g3d_bevfusion
