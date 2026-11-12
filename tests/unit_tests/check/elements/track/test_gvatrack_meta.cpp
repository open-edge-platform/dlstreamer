/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file test_gvatrack_meta.cpp
 * @brief Metadata-level tests for the gvatrack element.
 *
 * Frames with and without detections are pushed through gvatrack (imageless tracking, so pixel data is
 * irrelevant) to emulate gvadetect with inference-interval > 1. The tests check that the regions gvatrack
 * generates for frames without an associated detection carry the data of the last associated detection:
 * confidence and per-object tensors such as yolo-pose keypoints.
 */

#include <gtest/gtest.h>

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>

#include "dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h"
#include "video_frame.h"

#include <vector>

namespace {

constexpr int WIDTH = 640;
constexpr int HEIGHT = 480;
constexpr int LABEL_ID = 0;
constexpr const char *LABEL = "person";
constexpr int KEYPOINT_COUNT = 17;
// Number of frames with detections pushed before the tracker is expected to report a track
constexpr int WARMUP_FRAMES = 5;

struct Box {
    int x, y, w, h;
};

struct Detection {
    Box box;
    double confidence;
    // Keypoint x/y positions relative to the bounding box, empty means no keypoints tensor
    std::vector<float> keypoints;
};

std::vector<float> make_keypoints(float offset) {
    std::vector<float> positions(KEYPOINT_COUNT * 2);
    for (int k = 0; k < KEYPOINT_COUNT; k++) {
        positions[k * 2] = offset + 0.02f * k;
        positions[k * 2 + 1] = offset + 0.03f * k;
    }
    return positions;
}

// Builds a keypoints tensor in the same layout as the yolo-pose converters produce
GstStructure *make_keypoints_structure(const std::vector<float> &positions) {
    const GstAnalyticsKeypointDescriptor *desc =
        gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);
    GstStructure *s = gst_structure_new_empty(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    GVA::Tensor tensor(s);
    tensor.set_name(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    tensor.set_type(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
    tensor.set_format(desc->semantic_tag);
    tensor.set_dims({static_cast<guint>(KEYPOINT_COUNT), 2});
    tensor.set_data(positions.data(), positions.size() * sizeof(float));
    tensor.set_precision(GVA::Tensor::Precision::FP32);
    tensor.set_vector<float>("confidence", std::vector<float>(KEYPOINT_COUNT, 0.75f));
    tensor.set_vector<std::string>("point_names",
                                   std::vector<std::string>(desc->point_names, desc->point_names + desc->point_count));
    tensor.set_vector<uint32_t>(
        "point_connections", std::vector<uint32_t>(desc->skeleton_connections,
                                                   desc->skeleton_connections + desc->skeleton_connection_count * 2));
    return s;
}

const GVA::Tensor *find_keypoints(const std::vector<GVA::Tensor> &tensors) {
    for (const GVA::Tensor &tensor : tensors) {
        if (tensor.type() == GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR)
            return &tensor;
    }
    return nullptr;
}

size_t count_keypoints(const std::vector<GVA::Tensor> &tensors) {
    size_t count = 0;
    for (const GVA::Tensor &tensor : tensors) {
        if (tensor.type() == GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR)
            count++;
    }
    return count;
}

void expect_keypoints(const GVA::RegionOfInterest &roi, const std::vector<float> &expected) {
    const std::vector<GVA::Tensor> tensors = roi.tensors();
    const GVA::Tensor *keypoints = find_keypoints(tensors);
    ASSERT_NE(keypoints, nullptr) << "Region " << roi.object_id() << " has no keypoints";
    EXPECT_EQ(count_keypoints(tensors), 1u);
    EXPECT_EQ(keypoints->format(), std::string(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17));

    const std::vector<float> positions = keypoints->data<float>();
    ASSERT_EQ(positions.size(), expected.size());
    // Keypoints are stored in integer pixels in analytics metadata, so allow a pixel of rounding error
    for (size_t i = 0; i < positions.size(); i++)
        EXPECT_NEAR(positions[i], expected[i], 0.02f) << "position[" << i << "]";

    const std::vector<float> confidences = keypoints->confidences();
    ASSERT_EQ(confidences.size(), static_cast<size_t>(KEYPOINT_COUNT));
    for (float confidence : confidences)
        EXPECT_FLOAT_EQ(confidence, 0.75f);
}

class GvaTrackMetaTest : public ::testing::Test {
  protected:
    void SetUp() override {
        harness = gst_harness_new("gvatrack");
        ASSERT_NE(harness, nullptr);
        gst_util_set_object_arg(G_OBJECT(harness->element), "tracking-type", "short-term-imageless");
        gst_harness_set_src_caps_str(harness, "video/x-raw,format=BGRx,width=640,height=480,framerate=30/1");
        gst_video_info_set_format(&info, GST_VIDEO_FORMAT_BGRx, WIDTH, HEIGHT);
    }

    void TearDown() override {
        if (harness)
            gst_harness_teardown(harness);
    }

    // Pushes a frame with the given detections and returns the output buffer (caller owns it)
    GstBuffer *push(const std::vector<Detection> &detections) {
        GstBuffer *buffer = gst_harness_create_buffer(harness, GST_VIDEO_INFO_SIZE(&info));
        GST_BUFFER_PTS(buffer) = frame_index++ * GST_SECOND / 30;
        {
            GVA::VideoFrame frame(buffer, &info);
            for (const Detection &detection : detections) {
                GVA::RegionOfInterest roi = frame.add_region(detection.box.x, detection.box.y, detection.box.w,
                                                             detection.box.h, LABEL, detection.confidence);
                roi.detection().set_int("label_id", LABEL_ID);
                if (!detection.keypoints.empty())
                    roi.add_tensor(GVA::Tensor(make_keypoints_structure(detection.keypoints)));
            }
        }
        EXPECT_EQ(gst_harness_push(harness, buffer), GST_FLOW_OK);
        return gst_harness_pull(harness);
    }

    // Pushes frames with detections until every detection gets a tracking id
    void warmup(const std::vector<Detection> &detections) {
        for (int i = 0; i < WARMUP_FRAMES; i++)
            gst_buffer_unref(push(detections));
    }

    // Returns regions of the output buffer, checking that each one is tracked
    std::vector<GVA::RegionOfInterest> tracked_regions(GstBuffer *buffer) {
        GVA::VideoFrame frame(buffer, &info);
        std::vector<GVA::RegionOfInterest> regions = frame.regions();
        for (const GVA::RegionOfInterest &roi : regions)
            EXPECT_NE(roi.object_id(), 0) << "Region without tracking id";
        return regions;
    }

    GstHarness *harness = nullptr;
    GstVideoInfo info;
    guint64 frame_index = 0;
};

const Box BOX_A = {100, 80, 120, 240};
const Box BOX_B = {400, 100, 100, 200};

} // namespace

// Region generated by the tracker on a frame without detections keeps the keypoints of the detection
TEST_F(GvaTrackMetaTest, KeypointsCarriedToRegionWithoutDetection) {
    const std::vector<float> keypoints = make_keypoints(0.1f);
    warmup({{BOX_A, 0.9, keypoints}});

    GstBuffer *out = push({});
    std::vector<GVA::RegionOfInterest> regions = tracked_regions(out);
    ASSERT_EQ(regions.size(), 1u);
    expect_keypoints(regions[0], keypoints);
    gst_buffer_unref(out);
}

// The keypoints stay available for every frame until the next detection, not just the first one
TEST_F(GvaTrackMetaTest, KeypointsCarriedOverConsecutiveFramesWithoutDetection) {
    const std::vector<float> keypoints = make_keypoints(0.1f);
    warmup({{BOX_A, 0.9, keypoints}});

    for (int i = 0; i < 3; i++) {
        GstBuffer *out = push({});
        std::vector<GVA::RegionOfInterest> regions = tracked_regions(out);
        ASSERT_EQ(regions.size(), 1u) << "frame " << i;
        expect_keypoints(regions[0], keypoints);
        gst_buffer_unref(out);
    }
}

// The keypoints of the latest associated detection are carried, not the ones of earlier detections
TEST_F(GvaTrackMetaTest, KeypointsUpdatedByNewDetection) {
    const std::vector<float> old_keypoints = make_keypoints(0.1f);
    const std::vector<float> new_keypoints = make_keypoints(0.3f);
    warmup({{BOX_A, 0.9, old_keypoints}});
    gst_buffer_unref(push({{BOX_A, 0.9, new_keypoints}}));

    GstBuffer *out = push({});
    std::vector<GVA::RegionOfInterest> regions = tracked_regions(out);
    ASSERT_EQ(regions.size(), 1u);
    expect_keypoints(regions[0], new_keypoints);
    gst_buffer_unref(out);
}

// Regions of associated detections are only assigned a tracking id: their keypoints are not duplicated
TEST_F(GvaTrackMetaTest, DetectedRegionKeypointsNotDuplicated) {
    const std::vector<float> keypoints = make_keypoints(0.1f);
    warmup({{BOX_A, 0.9, keypoints}});
    gst_buffer_unref(push({}));

    GstBuffer *out = push({{BOX_A, 0.9, keypoints}});
    std::vector<GVA::RegionOfInterest> regions = tracked_regions(out);
    ASSERT_EQ(regions.size(), 1u);
    expect_keypoints(regions[0], keypoints);
    gst_buffer_unref(out);
}

// Each track carries its own data; a track whose detection had no keypoints does not get any
TEST_F(GvaTrackMetaTest, TensorsCarriedPerTrack) {
    const std::vector<float> keypoints = make_keypoints(0.1f);
    warmup({{BOX_A, 0.9, keypoints}, {BOX_B, 0.6, {}}});

    GstBuffer *out = push({});
    std::vector<GVA::RegionOfInterest> regions = tracked_regions(out);
    ASSERT_EQ(regions.size(), 2u);
    for (const GVA::RegionOfInterest &roi : regions) {
        const auto rect = roi.rect();
        if (rect.x < WIDTH / 2) {
            expect_keypoints(roi, keypoints);
        } else {
            EXPECT_EQ(find_keypoints(roi.tensors()), nullptr) << "Keypoints leaked to another track";
        }
    }
    gst_buffer_unref(out);
}

// Confidence of the last associated detection is carried to the region generated by the tracker
TEST_F(GvaTrackMetaTest, ConfidenceCarriedToRegionWithoutDetection) {
    warmup({{BOX_A, 0.9, {}}, {BOX_B, 0.6, {}}});
    gst_buffer_unref(push({{BOX_A, 0.8, {}}, {BOX_B, 0.5, {}}}));

    GstBuffer *out = push({});
    std::vector<GVA::RegionOfInterest> regions = tracked_regions(out);
    ASSERT_EQ(regions.size(), 2u);
    for (GVA::RegionOfInterest &roi : regions) {
        const double expected = roi.rect().x < WIDTH / 2 ? 0.8 : 0.5;
        EXPECT_NEAR(roi.confidence(), expected, 1e-6);
        EXPECT_EQ(roi.label(), std::string(LABEL));
        EXPECT_EQ(roi.detection().get_int("label_id", -1), LABEL_ID);
    }
    gst_buffer_unref(out);
}

GTEST_API_ int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    gst_check_init(&argc, &argv);
    return RUN_ALL_TESTS();
}
