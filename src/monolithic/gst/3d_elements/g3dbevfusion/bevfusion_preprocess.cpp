/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Host-side stages of BEVFusion, ported from run_inference_standalone.py in
// openvino_contrib/modules/openvino_bevfusion. Arithmetic follows the Python
// dtypes (float32 where numpy computes in float32, double where a float64
// constant promotes the expression) so results can be compared against it.

#include "bevfusion_preprocess.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>

namespace g3d_bevfusion {

namespace {

std::array<double, 9> invert3(const std::array<double, 9> &m) {
    const double det =
        m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (det == 0.0)
        throw std::runtime_error("singular 3x3 matrix");
    const double inv = 1.0 / det;
    return {(m[4] * m[8] - m[5] * m[7]) * inv, (m[2] * m[7] - m[1] * m[8]) * inv, (m[1] * m[5] - m[2] * m[4]) * inv,
            (m[5] * m[6] - m[3] * m[8]) * inv, (m[0] * m[8] - m[2] * m[6]) * inv, (m[2] * m[3] - m[0] * m[5]) * inv,
            (m[3] * m[7] - m[4] * m[6]) * inv, (m[1] * m[6] - m[0] * m[7]) * inv, (m[0] * m[4] - m[1] * m[3]) * inv};
}

// Inverse of an affine 4x4 (last row 0 0 0 1).
std::array<double, 16> invert_affine(const Mat4 &m) {
    const std::array<double, 9> r = {m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]};
    const std::array<double, 9> ri = invert3(r);
    const double t[3] = {m[3], m[7], m[11]};
    std::array<double, 16> out{};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            out[i * 4 + j] = ri[i * 3 + j];
        out[i * 4 + 3] = -(ri[i * 3] * t[0] + ri[i * 3 + 1] * t[1] + ri[i * 3 + 2] * t[2]);
    }
    out[15] = 1.0;
    return out;
}

} // namespace

Mat4 preprocess_image(const ImageView &image, const ModelParams &params, float *out) {
    if (!image.data || image.width <= 0 || image.height <= 0)
        throw std::invalid_argument("empty camera image");

    const int nw = static_cast<int>(image.width * params.resize_lim);
    const int nh = static_cast<int>(image.height * params.resize_lim);
    if (nw < kImageW || nh < kImageH)
        throw std::invalid_argument("camera image " + std::to_string(image.width) + "x" + std::to_string(image.height) +
                                    " is too small for a " + std::to_string(kImageW) + "x" + std::to_string(kImageH) +
                                    " crop after resizing by " + std::to_string(params.resize_lim));

    const cv::Mat src(image.height, image.width, CV_8UC3, const_cast<uint8_t *>(image.data), image.stride);
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(nw, nh)); // INTER_LINEAR, as cv2.resize
    const int crop_h = nh - kImageH;            // keep the bottom rows
    const int crop_w = (nw - kImageW) / 2;      // centre horizontally

    const size_t plane = static_cast<size_t>(kImageH) * kImageW;
    for (int y = 0; y < kImageH; y++) {
        const uint8_t *row = resized.ptr<uint8_t>(y + crop_h) + 3 * crop_w;
        for (int x = 0; x < kImageW; x++) {
            const size_t o = static_cast<size_t>(y) * kImageW + x;
            for (int c = 0; c < 3; c++) {
                // Output channel c is RGB; the source pixel is BGR.
                const float v = static_cast<float>(row[3 * x + 2 - c]) / 255.0f;
                out[c * plane + o] = (v - params.mean[c]) / params.std[c];
            }
        }
    }

    Mat4 aug{};
    aug[0] = static_cast<float>(params.resize_lim);
    aug[5] = static_cast<float>(params.resize_lim);
    aug[10] = 1.0f;
    aug[15] = 1.0f;
    aug[3] = static_cast<float>(-crop_w);
    aug[7] = static_cast<float>(-crop_h);
    return aug;
}

Mat4 lidar2image(const CameraCalibration &camera) {
    const std::array<double, 16> l2c = invert_affine(camera.camera2lidar);
    const Mat3 &k = camera.intrinsics;
    const double view[16] = {k[0], k[1], k[2], 0, k[3], k[4], k[5], 0, k[6], k[7], k[8], 0, 0, 0, 0, 1};
    Mat4 out{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0.0;
            for (int t = 0; t < 4; t++)
                s += view[i * 4 + t] * l2c[t * 4 + j];
            out[i * 4 + j] = static_cast<float>(s);
        }
    return out;
}

void depth_maps(const float *points, size_t num_points, size_t point_stride, const std::array<Mat4, kNumCameras> &l2i,
                const std::array<Mat4, kNumCameras> &aug, const ModelParams &params, float *out) {
    const size_t plane = static_cast<size_t>(kImageH) * kImageW;
    std::fill(out, out + kNumCameras * plane, 0.0f);
    const float dmin = static_cast<float>(params.dbound[0]);
    const float dmax = static_cast<float>(params.dbound[1]);

    for (int c = 0; c < kNumCameras; c++) {
        const Mat4 &m = l2i[c];
        const float sx = aug[c][0], sy = aug[c][5], tx = aug[c][3], ty = aug[c][7];
        float *dst = out + c * plane;
        for (size_t n = 0; n < num_points; n++) {
            const float *p = points + n * point_stride;
            const float px_h = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
            const float py_h = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
            const float depth = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
            if (!(depth > 0.1f))
                continue;
            const float ax = sx * (px_h / std::max(depth, 0.1f)) + tx;
            const float ay = sy * (py_h / std::max(depth, 0.1f)) + ty;
            if (!(ax >= 0.0f && ax < kImageW && ay >= 0.0f && ay < kImageH && depth >= dmin && depth < dmax))
                continue;
            // Later points overwrite earlier ones at the same pixel, as numpy's
            // fancy-index assignment does.
            dst[static_cast<size_t>(static_cast<int>(ay)) * kImageW + static_cast<int>(ax)] = depth;
        }
    }
}

void pack_geometry(const std::array<Mat4, kNumCameras> &aug, const std::array<CameraCalibration, kNumCameras> &cams,
                   float *combined, float *rotation) {
    for (int c = 0; c < kNumCameras; c++) {
        const Mat4 &a = aug[c];
        const std::array<double, 9> inv = invert3({a[0], a[1], a[2], a[4], a[5], a[6], a[8], a[9], a[10]});
        const Mat3 &k = cams[c].intrinsics;
        const Mat4 &t = cams[c].camera2lidar;
        float *row = combined + c * 14;
        row[0] = static_cast<float>(inv[0]);
        row[1] = static_cast<float>(inv[1]);
        row[2] = static_cast<float>(inv[3]);
        row[3] = static_cast<float>(inv[4]);
        row[4] = a[3];
        row[5] = a[7];
        row[6] = k[0]; // fx
        row[7] = k[4]; // fy
        row[8] = k[2]; // cx
        row[9] = k[5]; // cy
        row[10] = t[3];
        row[11] = t[7];
        row[12] = t[11];
        row[13] = 0.0f;
        float *r = rotation + c * 9;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                r[i * 3 + j] = t[i * 4 + j];
    }
}

int pack_points(const float *points, size_t num_points, size_t point_stride, float *out) {
    const int n = static_cast<int>(std::min<size_t>(num_points, kMaxPoints));
    std::fill(out, out + static_cast<size_t>(kMaxPoints) * kPointDims, 0.0f);
    for (int i = 0; i < n; i++) {
        const float *p = points + static_cast<size_t>(i) * point_stride;
        float *o = out + static_cast<size_t>(i) * kPointDims;
        o[0] = p[0];
        o[1] = p[1];
        o[2] = p[2];
        o[3] = p[3];
        o[4] = 0.0f; // key frame: no sweep time offset
    }
    return n;
}

int unpack_voxels(const float *result, float *features, int32_t *coords) {
    std::fill(features, features + static_cast<size_t>(kMaxVoxels) * 5, 0.0f);
    std::fill(coords, coords + static_cast<size_t>(kMaxVoxels) * 4, 0);
    const int n = std::min(static_cast<int>(result[0]), kMaxVoxels);
    for (int i = 0; i < n; i++) {
        const float *row = result + static_cast<size_t>(i + 1) * kVoxelColumns;
        std::memcpy(features + static_cast<size_t>(i) * 5, row, 5 * sizeof(float));
        for (int j = 0; j < 4; j++)
            coords[static_cast<size_t>(i) * 4 + j] = static_cast<int32_t>(row[5 + j]);
    }
    return std::max(n, 0);
}

std::vector<Box3D> decode(const HeadOutputs &head, const ModelParams &params, float score_threshold) {
    const int P = kNumProposals;
    const size_t KP = static_cast<size_t>(kNumClasses) * P;
    if (head.center.size() < 2u * P || head.height.size() < static_cast<size_t>(P) || head.dim.size() < 3u * P ||
        head.rot.size() < 2u * P || head.vel.size() < 2u * P || head.heatmap.size() < KP ||
        head.query_heatmap_score.size() < KP || head.top_proposals_class.size() < static_cast<size_t>(P))
        throw std::invalid_argument("detection head outputs are smaller than " + std::to_string(P) + " proposals");
    const std::array<double, 6> &r = params.post_center_range;
    std::vector<Box3D> boxes;
    for (int i = 0; i < P; i++) {
        const int cls = static_cast<int>(head.top_proposals_class[i]);
        float score = 0.0f;
        int label = 0;
        if (cls >= 0 && cls < kNumClasses) {
            const float h = head.heatmap[cls * P + i];
            const float sig = 1.0f / (1.0f + std::exp(-h));
            const float v = sig * head.query_heatmap_score[cls * P + i];
            // argmax over a one-hot product: ties at 0 resolve to class 0.
            if (v > 0.0f) {
                score = v;
                label = cls;
            }
        }
        if (score < score_threshold)
            continue;

        const double cx =
            static_cast<double>(head.center[i] * static_cast<float>(params.out_size_factor)) * params.voxel_size[0] +
            params.point_cloud_range[0];
        const double cy = static_cast<double>(head.center[P + i] * static_cast<float>(params.out_size_factor)) *
                              params.voxel_size[1] +
                          params.point_cloud_range[1];
        const float dx = std::exp(head.dim[i]);
        const float dy = std::exp(head.dim[P + i]);
        const float dz = std::exp(head.dim[2 * P + i]);
        const float cz = head.height[i] - dz * 0.5f;
        if (cx < r[0] || cx > r[3] || cy < r[1] || cy > r[4] || cz < r[2] || cz > r[5])
            continue;

        boxes.push_back({static_cast<float>(cx), static_cast<float>(cy), cz, dx, dy, dz,
                         std::atan2(head.rot[i], head.rot[P + i]), head.vel[i], head.vel[P + i], score, label});
    }
    std::stable_sort(boxes.begin(), boxes.end(), [](const Box3D &a, const Box3D &b) { return a.score > b.score; });
    return boxes;
}

} // namespace g3d_bevfusion
