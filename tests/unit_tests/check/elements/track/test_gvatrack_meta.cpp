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
 * confidence and per-object tensors such as yolo-pose keypoints. The tracking metadata of the regions is checked
 * for the first/last seen timestamps and the lost state of the track.
 */

#include <gtest/gtest.h>

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>

#include "dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h"
#include "video_frame.h"

#include <optional>
#include <vector>

namespace {

constexpr int WIDTH = 640;
constexpr int HEIGHT = 480;
constexpr int LABEL_ID = 0;
constexpr const char *LABEL = "person";
constexpr int KEYPOINT_COUNT = 17;
constexpr int FEATURE_SIZE = 128;
// Number of frames with detections pushed before the tracker is expected to report a track
constexpr int WARMUP_FRAMES = 5;
// Upper bound of frames without detections after which the tracker must have reported the track lost
constexpr int MAX_FRAMES_UNTIL_LOST = 300;

// Nonzero, so that a timestamp left uninitialized is noticed
GstClockTime frame_pts(guint64 index) {
    return (index + 1) * GST_SECOND / 30;
}

struct TrackingInfo {
    guint64 id;
    GstClockTime first_seen;
    GstClockTime last_seen;
    bool lost;
};

std::vector<TrackingInfo> tracking_infos(GstBuffer *buffer) {
    std::vector<TrackingInfo> infos;
    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
    if (!relation_meta)
        return infos;
    gpointer state = nullptr;
    GstAnalyticsTrackingMtd mtd;
    while (
        gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_tracking_mtd_get_mtd_type(), &mtd)) {
        TrackingInfo info;
        gboolean lost;
        EXPECT_TRUE(gst_analytics_tracking_mtd_get_info(&mtd, &info.id, &info.first_seen, &info.last_seen, &lost));
        info.lost = lost;
        infos.push_back(info);
    }
    return infos;
}

struct Box {
    int x, y, w, h;
};

struct Detection {
    Box box;
    double confidence;
    // Keypoint x/y positions relative to the bounding box, empty means no keypoints tensor
    std::vector<float> keypoints;
    // Appearance feature as produced by gvainference for Deep SORT, empty means no feature tensor
    std::vector<float> feature;
};

std::vector<float> make_feature(float seed) {
    std::vector<float> feature(FEATURE_SIZE);
    for (int i = 0; i < FEATURE_SIZE; i++)
        feature[i] = 1.0f + seed * (i % 7);
    return feature;
}

GstStructure *make_feature_structure(const std::vector<float> &feature) {
    GstStructure *s = gst_structure_new_empty("inference_layer_name:output");
    GVA::Tensor tensor(s);
    tensor.set_type(GVA::GST_ANALYTICS_TENSOR_2_TENSOR);
    tensor.set_layer_name("output");
    tensor.set_string("tensor_name", "output");
    tensor.set_dims({1, static_cast<guint>(FEATURE_SIZE)});
    tensor.set_data(feature.data(), feature.size() * sizeof(float));
    tensor.set_precision(GVA::Tensor::Precision::FP32);
    return s;
}

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
        configure_tracker();
        gst_harness_set_src_caps_str(harness, "video/x-raw,format=BGRx,width=640,height=480,framerate=30/1");
        gst_video_info_set_format(&info, GST_VIDEO_FORMAT_BGRx, WIDTH, HEIGHT);
    }

    virtual void configure_tracker() {
        gst_util_set_object_arg(G_OBJECT(harness->element), "tracking-type", "short-term-imageless");
    }

    void TearDown() override {
        if (harness)
            gst_harness_teardown(harness);
    }

    // Pushes a frame with the given detections and returns the output buffer (caller owns it)
    GstBuffer *push(const std::vector<Detection> &detections) {
        return push_with_pts(detections, frame_pts(frame_index++));
    }

    GstBuffer *push_with_pts(const std::vector<Detection> &detections, GstClockTime pts) {
        GstBuffer *buffer = gst_harness_create_buffer(harness, GST_VIDEO_INFO_SIZE(&info));
        GST_BUFFER_PTS(buffer) = pts;
        {
            GVA::VideoFrame frame(buffer, &info);
            for (const Detection &detection : detections) {
                GVA::RegionOfInterest roi = frame.add_region(detection.box.x, detection.box.y, detection.box.w,
                                                             detection.box.h, LABEL, detection.confidence);
                roi.detection().set_int("label_id", LABEL_ID);
                if (!detection.keypoints.empty())
                    roi.add_tensor(GVA::Tensor(make_keypoints_structure(detection.keypoints)));
                if (!detection.feature.empty())
                    roi.add_tensor(GVA::Tensor(make_feature_structure(detection.feature)));
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

// first_seen stays at the start of the track, last_seen follows the timestamp of the current frame
TEST_F(GvaTrackMetaTest, FirstAndLastSeenFollowFrameTimestamps) {
    warmup({{BOX_A, 0.9, {}}});

    GstBuffer *out = push({{BOX_A, 0.9, {}}});
    std::vector<TrackingInfo> infos = tracking_infos(out);
    ASSERT_EQ(infos.size(), 1u);
    const guint64 id = infos[0].id;
    const GstClockTime first_seen = infos[0].first_seen;
    EXPECT_GE(first_seen, frame_pts(0));
    EXPECT_LE(first_seen, frame_pts(WARMUP_FRAMES - 1));
    EXPECT_EQ(infos[0].last_seen, GST_BUFFER_PTS(out));
    EXPECT_FALSE(infos[0].lost);
    gst_buffer_unref(out);

    for (int i = 0; i < 3; i++) {
        out = push({{BOX_A, 0.9, {}}});
        infos = tracking_infos(out);
        ASSERT_EQ(infos.size(), 1u) << "frame " << i;
        EXPECT_EQ(infos[0].id, id);
        EXPECT_EQ(infos[0].first_seen, first_seen);
        EXPECT_EQ(infos[0].last_seen, GST_BUFFER_PTS(out));
        gst_buffer_unref(out);
    }
}

// Once the tracker loses the object, a single lost TrackingMtd is added to the buffer of the frame it happens on
TEST_F(GvaTrackMetaTest, LostTrackReportedOnce) {
    warmup({{BOX_A, 0.9, {}}});
    GstBuffer *out = push({{BOX_A, 0.9, {}}});
    const std::vector<TrackingInfo> tracked = tracking_infos(out);
    ASSERT_EQ(tracked.size(), 1u);
    const GstClockTime last_detected_pts = GST_BUFFER_PTS(out);
    gst_buffer_unref(out);

    int lost_reports = 0;
    for (int i = 0; i < MAX_FRAMES_UNTIL_LOST; i++) {
        out = push({});
        for (const TrackingInfo &info : tracking_infos(out)) {
            if (!info.lost)
                continue;
            lost_reports++;
            EXPECT_EQ(info.id, tracked[0].id);
            EXPECT_EQ(info.first_seen, tracked[0].first_seen);
            EXPECT_GE(info.last_seen, last_detected_pts);
            EXPECT_LT(info.last_seen, GST_BUFFER_PTS(out));
        }
        gst_buffer_unref(out);
    }
    EXPECT_EQ(lost_reports, 1);
}

// Buffers without PTS must not break tracking, timestamps of such tracks are reported as 0
TEST_F(GvaTrackMetaTest, BuffersWithoutPtsReportZeroTimestamps) {
    for (int i = 0; i < WARMUP_FRAMES; i++)
        gst_buffer_unref(push_with_pts({{BOX_A, 0.9, {}}}, GST_CLOCK_TIME_NONE));

    GstBuffer *out = push_with_pts({{BOX_A, 0.9, {}}}, GST_CLOCK_TIME_NONE);
    std::vector<TrackingInfo> infos = tracking_infos(out);
    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].first_seen, 0u);
    EXPECT_EQ(infos[0].last_seen, 0u);
    EXPECT_FALSE(infos[0].lost);
    gst_buffer_unref(out);
}

// Deep SORT confirms a track after n_init hits and deletes it after max_age missed frames
class GvaTrackDeepSortTest : public GvaTrackMetaTest {
  protected:
    void configure_tracker() override {
        gst_util_set_object_arg(G_OBJECT(harness->element), "tracking-type", "deep-sort");
        g_object_set(harness->element, "deepsort-trck-cfg", "n_init=2,max_age=3,reid_max_age=100", nullptr);
    }

    // Pushes frames until a lost TrackingMtd shows up, returns it or nullopt
    std::optional<TrackingInfo> push_until_lost() {
        for (int i = 0; i < MAX_FRAMES_UNTIL_LOST; i++) {
            GstBuffer *out = push({});
            std::vector<TrackingInfo> infos = tracking_infos(out);
            gst_buffer_unref(out);
            for (const TrackingInfo &info : infos) {
                if (info.lost)
                    return info;
            }
        }
        return std::nullopt;
    }
};

TEST_F(GvaTrackDeepSortTest, FirstAndLastSeenFollowFrameTimestamps) {
    const std::vector<float> feature = make_feature(0.1f);
    const Detection det = {BOX_A, 0.9, {}, feature};

    gst_buffer_unref(push({det}));
    for (int i = 0; i < 3; i++) {
        GstBuffer *out = push({det});
        std::vector<TrackingInfo> infos = tracking_infos(out);
        ASSERT_EQ(infos.size(), 1u) << "frame " << i;
        EXPECT_EQ(infos[0].first_seen, frame_pts(0));
        EXPECT_EQ(infos[0].last_seen, GST_BUFFER_PTS(out));
        EXPECT_FALSE(infos[0].lost);
        gst_buffer_unref(out);
    }
}

TEST_F(GvaTrackDeepSortTest, LostTrackReportedOnce) {
    const Detection det = {BOX_A, 0.9, {}, make_feature(0.1f)};
    GstClockTime last_detected_pts = 0;
    guint64 id = 0;
    for (int i = 0; i < 4; i++) {
        GstBuffer *out = push({det});
        last_detected_pts = GST_BUFFER_PTS(out);
        std::vector<TrackingInfo> infos = tracking_infos(out);
        if (!infos.empty())
            id = infos[0].id;
        gst_buffer_unref(out);
    }
    ASSERT_NE(id, 0u);

    std::optional<TrackingInfo> lost = push_until_lost();
    ASSERT_TRUE(lost.has_value());
    EXPECT_EQ(lost->id, id);
    EXPECT_EQ(lost->first_seen, frame_pts(0));
    EXPECT_EQ(lost->last_seen, last_detected_pts);

    EXPECT_FALSE(push_until_lost().has_value()) << "Track reported lost more than once";
}

// A track whose id is reacquired through re-ID keeps the original first_seen
TEST_F(GvaTrackDeepSortTest, ReacquiredIdKeepsFirstSeen) {
    const Detection det = {BOX_A, 0.9, {}, make_feature(0.1f)};
    guint64 id = 0;
    for (int i = 0; i < 4; i++) {
        GstBuffer *out = push({det});
        std::vector<TrackingInfo> infos = tracking_infos(out);
        if (!infos.empty())
            id = infos[0].id;
        gst_buffer_unref(out);
    }
    ASSERT_NE(id, 0u);
    ASSERT_TRUE(push_until_lost().has_value());

    for (int i = 0; i < 3; i++) {
        GstBuffer *out = push({det});
        std::vector<TrackingInfo> infos = tracking_infos(out);
        if (i > 0) {
            ASSERT_EQ(infos.size(), 1u) << "frame " << i;
            EXPECT_EQ(infos[0].id, id);
            EXPECT_EQ(infos[0].first_seen, frame_pts(0));
            EXPECT_EQ(infos[0].last_seen, GST_BUFFER_PTS(out));
            EXPECT_FALSE(infos[0].lost);
        }
        gst_buffer_unref(out);
    }
}

// A track first seen on buffers without PTS gets first_seen from the first buffer with a valid PTS
TEST_F(GvaTrackDeepSortTest, FirstSeenSetByFirstValidPts) {
    const Detection det = {BOX_A, 0.9, {}, make_feature(0.1f)};

    for (int i = 0; i < 2; i++) {
        GstBuffer *out = push_with_pts({det}, GST_CLOCK_TIME_NONE);
        for (const TrackingInfo &info : tracking_infos(out)) {
            EXPECT_EQ(info.first_seen, 0u);
            EXPECT_EQ(info.last_seen, 0u);
        }
        gst_buffer_unref(out);
    }

    // The first buffer with a valid PTS is frame_pts(0)
    for (int i = 0; i < 3; i++) {
        GstBuffer *out = push({det});
        std::vector<TrackingInfo> infos = tracking_infos(out);
        ASSERT_EQ(infos.size(), 1u) << "frame " << i;
        EXPECT_EQ(infos[0].first_seen, frame_pts(0));
        EXPECT_EQ(infos[0].last_seen, GST_BUFFER_PTS(out));
        gst_buffer_unref(out);
    }
}

// PTS 0 is a valid timestamp: first_seen must stay 0 when later frames have a greater PTS
TEST_F(GvaTrackDeepSortTest, ZeroPtsIsValidFirstSeen) {
    const Detection det = {BOX_A, 0.9, {}, make_feature(0.1f)};

    gst_buffer_unref(push_with_pts({det}, 0));
    for (int i = 0; i < 3; i++) {
        GstBuffer *out = push({det});
        std::vector<TrackingInfo> infos = tracking_infos(out);
        ASSERT_EQ(infos.size(), 1u) << "frame " << i;
        EXPECT_EQ(infos[0].first_seen, 0u);
        EXPECT_EQ(infos[0].last_seen, GST_BUFFER_PTS(out));
        gst_buffer_unref(out);
    }
}

GTEST_API_ int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    gst_check_init(&argc, &argv);
    return RUN_ALL_TESTS();
}
