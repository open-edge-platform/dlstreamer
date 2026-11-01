/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "bevfusion_preprocess.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace g3d_bevfusion {

// One frame of sensor data. Points are (x, y, z, intensity, ...) floats in
// the lidar frame; cameras are given in the model's slot order and are
// optional -- without them the frame runs lidar-only.
struct FrameInput {
    const float *points = nullptr;
    size_t num_points = 0;
    size_t point_stride = 4; // floats per point
    std::optional<std::array<ImageView, kNumCameras>> cameras;
};

// Runs the OpenVINO BEVFusion model exported by openvino_contrib
// (modules/openvino_bevfusion) on one frame at a time.
//
// The runtime config is a JSON file:
//   {
//     "model_dir":         "<dir with the exported IRs and config.json>",
//     "extensions":        ["<libopenvino_bevpool_extension.so>", ...],
//     "gpu_custom_layers": "<openvino_extensions/gpu_custom_layers.xml>",
//     "kernel_dir":        "<dir with sparse_conv.cl>"            (optional)
//   }
// Relative paths are resolved against the config file's directory.
class Runtime {
  public:
    Runtime(const std::string &config_path, const std::string &device);
    ~Runtime();
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;

    void set_calibration(const std::array<CameraCalibration, kNumCameras> &cameras);
    std::vector<Box3D> infer(const FrameInput &frame, float score_threshold);

    const ModelParams &params() const;
    // When set, each frame's decoded boxes are written there as
    // boxes_<frame>.npy (float32 [N, 11]: x, y, z_bottom, dx, dy, dz, yaw, vx,
    // vy, score, label) for comparison against the Python runtime; with
    // `tensors`, every stage's input/output is written as <tag>_<frame>.npy too.
    void set_dump_dir(const std::string &dir, bool tensors = false);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace g3d_bevfusion
