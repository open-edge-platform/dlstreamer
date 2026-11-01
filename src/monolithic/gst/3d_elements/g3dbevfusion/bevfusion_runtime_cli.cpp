/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Development tool: run the BEVFusion runtime outside GStreamer on a sequence
// written by samples/gstreamer/gst_launch/g3dbevfusion/nuscenes_to_sequence.py
// and save boxes in the layout of bevfusion_reference.py, so the two can be
// compared frame by frame. Not installed.
//
//   bevfusion_runtime_cli <runtime-config.json> <scene-dir> <out-dir> [fusion|lidar] [frames] [device]
//
// Set G3DBEVFUSION_DUMP_DIR to also write every stage's tensors.

#include "bevfusion_runtime.h"

#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace g3d_bevfusion;

static std::array<CameraCalibration, kNumCameras> load_calibration(const fs::path &file) {
    std::ifstream f(file);
    const nlohmann::json cfg = nlohmann::json::parse(f);
    std::array<CameraCalibration, kNumCameras> cams{};
    for (const auto &[pad, cam] : cfg.at("cameras").items()) {
        const int slot = cam.at("slot").get<int>();
        CameraCalibration &c = cams.at(slot);
        c.name = cam.at("name").get<std::string>();
        const auto c2l = cam.at("camera2lidar").get<std::vector<double>>();
        const auto k = cam.at("intrinsics").get<std::vector<double>>();
        for (int i = 0; i < 16; i++)
            c.camera2lidar[i] = static_cast<float>(c2l.at(i));
        for (int i = 0; i < 9; i++)
            c.intrinsics[i] = static_cast<float>(k.at(i));
    }
    return cams;
}

static void save_boxes(const fs::path &path, const std::vector<Box3D> &boxes) {
    std::vector<float> rows;
    for (const Box3D &b : boxes)
        rows.insert(rows.end(),
                    {b.x, b.y, b.z_bottom, b.dx, b.dy, b.dz, b.yaw, b.vx, b.vy, b.score, static_cast<float>(b.label)});
    std::string header =
        "{'descr': '<f4', 'fortran_order': False, 'shape': (" + std::to_string(boxes.size()) + ", 11), }";
    header.append(63 - (10 + header.size()) % 64, ' ');
    header.push_back('\n');
    std::ofstream f(path, std::ios::binary);
    const uint16_t len = static_cast<uint16_t>(header.size());
    f.write("\x93NUMPY\x01\x00", 8);
    f.write(reinterpret_cast<const char *>(&len), 2);
    f.write(header.data(), header.size());
    f.write(reinterpret_cast<const char *>(rows.data()), rows.size() * sizeof(float));
}

int main(int argc, char **argv) {
    if (argc < 4) {
        std::cerr << "usage: " << argv[0]
                  << " <runtime-config.json> <scene-dir> <out-dir> [fusion|lidar] [frames] [device]\n";
        return 2;
    }
    const fs::path scene = argv[2], out_root = argv[3];
    const std::string mode = argc > 4 ? argv[4] : "fusion";
    int frames = argc > 5 ? std::atoi(argv[5]) : -1;
    const std::string device = argc > 6 ? argv[6] : "GPU";

    Runtime rt(argv[1], device);
    const auto cams = load_calibration(scene / "calibration.json");
    rt.set_calibration(cams);
    if (const char *dump = std::getenv("G3DBEVFUSION_DUMP_DIR"))
        rt.set_dump_dir(dump, /*tensors=*/true);

    const fs::path out = out_root / mode;
    fs::create_directories(out);
    for (int i = 0; frames < 0 || i < frames; i++) {
        char name[32];
        std::snprintf(name, sizeof(name), "%06d", i);
        const fs::path bin = scene / "LIDAR_TOP" / (std::string(name) + ".bin");
        if (!fs::exists(bin))
            break;
        std::vector<float> points(fs::file_size(bin) / sizeof(float));
        std::ifstream(bin, std::ios::binary).read(reinterpret_cast<char *>(points.data()), points.size() * 4);

        FrameInput in;
        in.points = points.data();
        in.num_points = points.size() / 4;
        in.point_stride = 4;
        std::array<cv::Mat, kNumCameras> mats;
        if (mode == "fusion") {
            std::array<ImageView, kNumCameras> views{};
            for (int c = 0; c < kNumCameras; c++) {
                mats[c] = cv::imread((scene / cams[c].name / (std::string(name) + ".jpg")).string(), cv::IMREAD_COLOR);
                if (mats[c].empty())
                    throw std::runtime_error("cannot read image for " + cams[c].name);
                views[c] = {mats[c].data, mats[c].cols, mats[c].rows, mats[c].step};
            }
            in.cameras = views;
        }

        const auto t0 = std::chrono::steady_clock::now();
        const std::vector<Box3D> boxes = rt.infer(in, 0.0f);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        save_boxes(out / ("boxes_" + std::string(name) + ".npy"), boxes);
        size_t strong = 0;
        for (const Box3D &b : boxes)
            strong += b.score > 0.3f;
        std::printf("%s %3d: %zu boxes, score>0.3: %zu, top score %.3f, %.0f ms\n", mode.c_str(), i, boxes.size(),
                    strong, boxes.empty() ? 0.0f : boxes[0].score, ms);
        std::fflush(stdout);
    }
    return 0;
}
