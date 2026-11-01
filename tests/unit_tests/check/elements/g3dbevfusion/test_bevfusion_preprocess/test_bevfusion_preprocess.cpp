/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* Host-side BEVFusion stages of g3dbevfusion (bevfusion_preprocess.h). These
 * need no model, so they run anywhere; the expected values are worked out by
 * hand from the formulas in openvino_bevfusion's run_inference_standalone.py. */

#include <gst/check/gstcheck.h>

#include "bevfusion_preprocess.h"

#include <cmath>
#include <stdexcept>
#include <vector>

using namespace g3d_bevfusion;

/* GStreamer's bundled libcheck predates ck_assert_float_eq*. */
#define assert_near(a, b, tol)                                                                                         \
    ck_assert_msg(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= (tol), "%s = %g, expected %g", #a,     \
                  static_cast<double>(a), static_cast<double>(b))

namespace {

/* camera2lidar of a camera looking along lidar +y (nuScenes CAM_FRONT-like):
 * camera x (right) = lidar +x, camera y (down) = lidar -z, camera z
 * (forward) = lidar +y; mounted 1.5 m up and 0.5 m forward. */
CameraCalibration front_camera() {
    CameraCalibration c;
    c.name = "front";
    c.camera2lidar = {1, 0, 0, 0, 0, 0, 1, 0.5f, 0, -1, 0, 1.5f, 0, 0, 0, 1};
    c.intrinsics = {1000, 0, 800, 0, 1000, 450, 0, 0, 1};
    return c;
}

/* Identity camera2lidar: the camera frame is the lidar frame. */
CameraCalibration identity_camera() {
    CameraCalibration c;
    c.camera2lidar = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    c.intrinsics = {1000, 0, 800, 0, 1000, 450, 0, 0, 1};
    return c;
}

ModelParams default_params() {
    ModelParams p;
    p.class_names.assign(kNumClasses, "class");
    return p;
}

Mat4 nuscenes_aug() {
    /* 1600x900 * 0.48 = 768x432; crop 704x256 at (32, 176). */
    return {0.48f, 0, 0, -32, 0, 0.48f, 0, -176, 0, 0, 1, 0, 0, 0, 0, 1};
}

} // namespace

GST_START_TEST(test_lidar2image_projects_like_k) {
    const CameraCalibration cam = front_camera();
    const Mat4 l2i = lidar2image(cam);
    /* A camera-frame point, taken into the lidar frame by camera2lidar, must
     * come back as K * point. */
    const float pc[3] = {2.0f, -1.0f, 10.0f};
    const Mat4 &t = cam.camera2lidar;
    float pl[3];
    for (int i = 0; i < 3; i++)
        pl[i] = t[i * 4] * pc[0] + t[i * 4 + 1] * pc[1] + t[i * 4 + 2] * pc[2] + t[i * 4 + 3];
    float img[3];
    for (int i = 0; i < 3; i++)
        img[i] = l2i[i * 4] * pl[0] + l2i[i * 4 + 1] * pl[1] + l2i[i * 4 + 2] * pl[2] + l2i[i * 4 + 3];
    assert_near(img[2], 10.0f, 1e-4f);
    assert_near(img[0] / img[2], 1000.0f * 2.0f / 10.0f + 800.0f, 1e-3f);
    assert_near(img[1] / img[2], 1000.0f * -1.0f / 10.0f + 450.0f, 1e-3f);
}
GST_END_TEST;

GST_START_TEST(test_preprocess_image_normalises_and_crops) {
    const int w = 1600, h = 900;
    std::vector<uint8_t> bgr(static_cast<size_t>(w) * h * 3);
    for (size_t i = 0; i < bgr.size(); i += 3) {
        bgr[i] = 10;     // B
        bgr[i + 1] = 20; // G
        bgr[i + 2] = 30; // R
    }
    const ModelParams p = default_params();
    std::vector<float> out(3 * kImageH * kImageW);
    const Mat4 aug = preprocess_image({bgr.data(), w, h, static_cast<size_t>(w) * 3}, p, out.data());

    const Mat4 want = nuscenes_aug();
    for (int i = 0; i < 16; i++)
        assert_near(aug[i], want[i], 1e-6f);

    /* Output channels are RGB, normalised with the ImageNet mean/std. */
    const size_t plane = static_cast<size_t>(kImageH) * kImageW;
    const float rgb[3] = {30.0f, 20.0f, 10.0f};
    for (int c = 0; c < 3; c++) {
        const float expected = (rgb[c] / 255.0f - p.mean[c]) / p.std[c];
        assert_near(out[c * plane], expected, 1e-6f);
        assert_near(out[c * plane + plane - 1], expected, 1e-6f);
    }
}
GST_END_TEST;

GST_START_TEST(test_preprocess_image_rejects_small_images) {
    const int w = 1241, h = 376; /* KITTI: 595x180 after resizing, smaller than the crop */
    std::vector<uint8_t> bgr(static_cast<size_t>(w) * h * 3, 0);
    std::vector<float> out(3 * kImageH * kImageW);
    bool threw = false;
    try {
        preprocess_image({bgr.data(), w, h, static_cast<size_t>(w) * 3}, default_params(), out.data());
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    ck_assert(threw);
}
GST_END_TEST;

GST_START_TEST(test_depth_maps_scatter_points) {
    std::array<Mat4, kNumCameras> l2i, aug;
    for (int c = 0; c < kNumCameras; c++) {
        l2i[c] = lidar2image(identity_camera());
        aug[c] = nuscenes_aug();
    }
    /* (x, y, z, intensity): on the optical axis at 10 m, too near (0.05 m),
     * beyond dbound (70 m), and behind the camera. */
    const float points[] = {0, 0, 10, 1, 0, 0, 0.05f, 1, 0, 0, 70, 1, 0, 0, -10, 1};
    std::vector<float> out(static_cast<size_t>(kNumCameras) * kImageH * kImageW, -1.0f);
    depth_maps(points, 4, 4, l2i, aug, default_params(), out.data());

    /* Principal point (800, 450) -> (800 * 0.48 - 32, 450 * 0.48 - 176) = (352, 40). */
    const size_t plane = static_cast<size_t>(kImageH) * kImageW;
    for (int c = 0; c < kNumCameras; c++) {
        size_t filled = 0;
        for (size_t i = 0; i < plane; i++)
            filled += out[c * plane + i] != 0.0f;
        ck_assert_uint_eq(filled, 1);
        assert_near(out[c * plane + 40 * kImageW + 352], 10.0f, 0.0);
    }
}
GST_END_TEST;

GST_START_TEST(test_pack_geometry_layout) {
    std::array<Mat4, kNumCameras> aug;
    std::array<CameraCalibration, kNumCameras> cams;
    for (int c = 0; c < kNumCameras; c++) {
        aug[c] = nuscenes_aug();
        cams[c] = front_camera();
    }
    std::vector<float> combined(kNumCameras * 14), rotation(kNumCameras * 9);
    pack_geometry(aug, cams, combined.data(), rotation.data());

    /* [augInv00, 01, 10, 11, aug_tx, aug_ty, fx, fy, cx, cy, tx, ty, tz, 0] */
    const float want[14] = {1 / 0.48f, 0, 0, 1 / 0.48f, -32, -176, 1000, 1000, 800, 450, 0, 0.5f, 1.5f, 0};
    const float want_rot[9] = {1, 0, 0, 0, 0, 1, 0, -1, 0};
    for (int c = 0; c < kNumCameras; c++) {
        for (int i = 0; i < 14; i++)
            assert_near(combined[c * 14 + i], want[i], 1e-5f);
        for (int i = 0; i < 9; i++)
            assert_near(rotation[c * 9 + i], want_rot[i], 0.0);
    }
}
GST_END_TEST;

GST_START_TEST(test_pack_points_adds_zero_time_and_truncates) {
    std::vector<float> in(static_cast<size_t>(kMaxPoints + 5) * 4);
    for (size_t i = 0; i < in.size(); i++)
        in[i] = static_cast<float>(i % 97) + 1.0f;
    std::vector<float> out(static_cast<size_t>(kMaxPoints) * kPointDims, -1.0f);

    ck_assert_int_eq(pack_points(in.data(), 2, 4, out.data()), 2);
    for (int j = 0; j < 4; j++)
        assert_near(out[5 + j], in[4 + j], 0.0);
    assert_near(out[4], 0.0f, 0.0);
    assert_near(out[9], 0.0f, 0.0);
    assert_near(out[10], 0.0f, 0.0); /* padding */

    ck_assert_int_eq(pack_points(in.data(), kMaxPoints + 5, 4, out.data()), kMaxPoints);
}
GST_END_TEST;

GST_START_TEST(test_unpack_voxels) {
    std::vector<float> result(static_cast<size_t>(kMaxVoxels + 1) * kVoxelColumns, 0.0f);
    result[0] = 2.0f;
    const float v0[kVoxelColumns] = {1, 2, 3, 4, 0, 0, 100, 200, 30};
    const float v1[kVoxelColumns] = {5, 6, 7, 8, 0, 0, 101, 202, 31};
    std::copy(v0, v0 + kVoxelColumns, result.begin() + kVoxelColumns);
    std::copy(v1, v1 + kVoxelColumns, result.begin() + 2 * kVoxelColumns);
    std::vector<float> features(static_cast<size_t>(kMaxVoxels) * 5, -1.0f);
    std::vector<int32_t> coords(static_cast<size_t>(kMaxVoxels) * 4, -1);

    ck_assert_int_eq(unpack_voxels(result.data(), features.data(), coords.data()), 2);
    assert_near(features[5], 5.0f, 0.0);
    assert_near(features[8], 8.0f, 0.0);
    ck_assert_int_eq(coords[0], 0);
    ck_assert_int_eq(coords[1], 100);
    ck_assert_int_eq(coords[7], 31);
    assert_near(features[10], 0.0f, 0.0); /* cleared past the voxel count */
    ck_assert_int_eq(coords[8], 0);
}
GST_END_TEST;

GST_START_TEST(test_decode_boxes) {
    const int P = kNumProposals;
    HeadOutputs h;
    h.center.assign(2 * P, 0.0f);
    h.height.assign(P, 0.0f);
    h.dim.assign(3 * P, 0.0f);
    h.rot.assign(2 * P, 0.0f);
    h.vel.assign(2 * P, 0.0f);
    h.heatmap.assign(static_cast<size_t>(kNumClasses) * P, 0.0f);
    h.query_heatmap_score.assign(static_cast<size_t>(kNumClasses) * P, 0.0f);
    h.top_proposals_class.assign(P, 0);

    /* Proposal 0: class 3, sigmoid(0) * 0.8 = 0.4; centre cell (90, 100) ->
     * (90 * 8 * 0.075 - 54, 100 * 8 * 0.075 - 54) = (0, 6); dims (4, 2, 1.5);
     * height 1 -> z_bottom 0.25; rot (sin, cos) = (1, 0) -> yaw pi/2. */
    h.top_proposals_class[0] = 3;
    h.query_heatmap_score[3 * P + 0] = 0.8f;
    h.center[0] = 90;
    h.center[P + 0] = 100;
    h.height[0] = 1.0f;
    h.dim[0] = std::log(4.0f);
    h.dim[P + 0] = std::log(2.0f);
    h.dim[2 * P + 0] = std::log(1.5f);
    h.rot[0] = 1.0f;
    h.vel[0] = 2.0f;
    h.vel[P + 0] = -1.0f;
    /* Proposal 1: higher score but centred at x = 70 m, outside post_center_range. */
    h.top_proposals_class[1] = 0;
    h.heatmap[0 * P + 1] = 5.0f;
    h.query_heatmap_score[0 * P + 1] = 1.0f;
    h.center[1] = 207;
    h.center[P + 1] = 90;
    /* Proposal 2: class 8, sigmoid(0) * 0.9 = 0.45, scores above proposal 0. */
    h.top_proposals_class[2] = 8;
    h.query_heatmap_score[8 * P + 2] = 0.9f;
    h.center[2] = 90;
    h.center[P + 2] = 90;

    const std::vector<Box3D> boxes = decode(h, default_params(), 0.1f);
    ck_assert_uint_eq(boxes.size(), 2);
    ck_assert_int_eq(boxes[0].label, 8); /* sorted by score */
    assert_near(boxes[0].score, 0.45f, 1e-6f);

    const Box3D &b = boxes[1];
    ck_assert_int_eq(b.label, 3);
    assert_near(b.score, 0.4f, 1e-6f);
    assert_near(b.x, 0.0f, 1e-4f);
    assert_near(b.y, 6.0f, 1e-4f);
    assert_near(b.z_bottom, 0.25f, 1e-5f);
    assert_near(b.dx, 4.0f, 1e-5f);
    assert_near(b.dy, 2.0f, 1e-5f);
    assert_near(b.dz, 1.5f, 1e-5f);
    assert_near(b.yaw, static_cast<float>(M_PI / 2), 1e-6f);
    assert_near(b.vx, 2.0f, 0.0);
    assert_near(b.vy, -1.0f, 0.0);
}
GST_END_TEST;

static Suite *bevfusion_preprocess_suite(void) {
    Suite *s = suite_create("bevfusion_preprocess");
    TCase *tc = tcase_create("preprocess");
    tcase_add_test(tc, test_lidar2image_projects_like_k);
    tcase_add_test(tc, test_preprocess_image_normalises_and_crops);
    tcase_add_test(tc, test_preprocess_image_rejects_small_images);
    tcase_add_test(tc, test_depth_maps_scatter_points);
    tcase_add_test(tc, test_pack_geometry_layout);
    tcase_add_test(tc, test_pack_points_adds_zero_time_and_truncates);
    tcase_add_test(tc, test_unpack_voxels);
    tcase_add_test(tc, test_decode_boxes);
    suite_add_tcase(s, tc);
    return s;
}

GST_CHECK_MAIN(bevfusion_preprocess);
