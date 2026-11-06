/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* TEST
 * Overview: Tests ROI collection, confidence and class filtering, aggregated ROI precedence, scaling,
 *           invalid crops, and element-level output counts, caps, pixels, metadata, and DISCONT handling.
 */

#include "roi_scale.hpp"
#include "gva_utils.h"
#include "gstgvaroiscale.h"

#include <gst/check/gstharness.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

namespace {

GArray *make_classes(std::initializer_list<const char *> labels) {
    GArray *classes = g_array_new(FALSE, FALSE, sizeof(GQuark));
    for (const char *label : labels) {
        const GQuark quark = g_quark_from_string(label);
        g_array_append_val(classes, quark);
    }
    return classes;
}

void add_detection(GstBuffer *buffer, const char *label, gint x, gint y, gint width, gint height, gfloat confidence) {
    GstAnalyticsRelationMeta *meta = gst_buffer_get_analytics_relation_meta(buffer);
    if (!meta)
        meta = gst_buffer_add_analytics_relation_meta(buffer);
    GstAnalyticsODMtd detection;
    EXPECT_TRUE(gst_analytics_relation_meta_add_od_mtd(meta, g_quark_from_string(label), x, y, width, height,
                                                       confidence, &detection));
}

GstBuffer *make_rgb_buffer(const GstVideoInfo &info, const std::vector<guint8> &pixels) {
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, info.size, nullptr);
    GstMapInfo map = GST_MAP_INFO_INIT;
    EXPECT_TRUE(gst_buffer_map(buffer, &map, GST_MAP_WRITE));
    std::memcpy(map.data, pixels.data(), std::min(static_cast<gsize>(map.size), pixels.size()));
    gst_buffer_unmap(buffer, &map);
    return buffer;
}

using BufferPtr = std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)>;

class GvaRoiScaleTest : public ::testing::Test {
  protected:
    void SetUp() override {
        GstElement *element = GST_ELEMENT(g_object_new(GST_TYPE_GVA_ROI_SCALE, "out-width", 4u, "out-height", 4u,
                                                       "target-classes", "person,vehicle", "confidence-threshold",
                                                       0.5, "output-dir", "", nullptr));
        harness = gst_harness_new_with_element(element, "sink", "src");
        gst_object_unref(element);
        ASSERT_NE(harness, nullptr);
        gst_harness_set_sink_caps_str(harness, "video/x-raw,format=RGB,width=4,height=4,framerate=30/1");
        gst_harness_set_src_caps_str(harness, "video/x-raw,format=RGB,width=4,height=2,framerate=30/1");
    }

    void TearDown() override {
        if (harness)
            gst_harness_teardown(harness);
    }

    GstBuffer *make_frame() {
        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, 24, nullptr);
        const guint8 pixels[] = {10, 20, 30, 10, 20, 30, 100, 110, 120, 100, 110, 120,
                                  10, 20, 30, 10, 20, 30, 100, 110, 120, 100, 110, 120};
        gst_buffer_fill(buffer, 0, pixels, sizeof(pixels));
        GST_BUFFER_PTS(buffer) = 123;
        GST_BUFFER_DTS(buffer) = 100;
        GST_BUFFER_DURATION(buffer) = 40;
        GST_BUFFER_OFFSET(buffer) = 7;
        return buffer;
    }

    void expect_detection(GstBuffer *buffer, const char *label, gfloat confidence, gint x, gint y, gint width,
                          gint height) {
        GstAnalyticsRelationMeta *meta = gst_buffer_get_analytics_relation_meta(buffer);
        ASSERT_NE(meta, nullptr);
        gpointer state = nullptr;
        GstAnalyticsODMtd detection;
        ASSERT_TRUE(gst_analytics_relation_meta_iterate(meta, &state, gst_analytics_od_mtd_get_mtd_type(), &detection));
        EXPECT_EQ(gst_analytics_od_mtd_get_obj_type(&detection), g_quark_from_string(label));
        gint actual_x = 0, actual_y = 0, actual_width = 0, actual_height = 0;
        gfloat actual_confidence = 0;
        ASSERT_TRUE(gst_analytics_od_mtd_get_location(&detection, &actual_x, &actual_y, &actual_width,
                                                     &actual_height, &actual_confidence));
        EXPECT_EQ(actual_x, x);
        EXPECT_EQ(actual_y, y);
        EXPECT_EQ(actual_width, width);
        EXPECT_EQ(actual_height, height);
        EXPECT_FLOAT_EQ(actual_confidence, confidence);
        EXPECT_FALSE(gst_analytics_relation_meta_iterate(meta, &state, gst_analytics_od_mtd_get_mtd_type(), &detection));
    }

    GstHarness *harness = nullptr;
};

/* TEST
 * Tests that the element drops frames without a qualifying ROI and does not emit them at EOS.
 * Input: three 4x2 RGB frames: no detections, cat at 0.99, and person at 0.4; then EOS.
 * Values: target-classes=person,vehicle, confidence-threshold=0.5, output=4x4 RGB.
 * Expected: the output queue remains empty after each frame and after EOS.
 */
TEST_F(GvaRoiScaleTest, NoMatchingRoiProducesNoOutput) {
    ASSERT_EQ(gst_harness_push(harness, make_frame()), GST_FLOW_OK);
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
    GstBuffer *wrong_class = make_frame();
    add_detection(wrong_class, "cat", 0, 0, 4, 2, 0.99f);
    ASSERT_EQ(gst_harness_push(harness, wrong_class), GST_FLOW_OK);
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
    GstBuffer *low_confidence = make_frame();
    add_detection(low_confidence, "person", 0, 0, 4, 2, 0.4f);
    ASSERT_EQ(gst_harness_push(harness, low_confidence), GST_FLOW_OK);
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
    ASSERT_TRUE(gst_harness_push_event(harness, gst_event_new_eos()));
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
}

/* TEST
 * Tests one-to-many output with independently scaled crops and rewritten detection metadata.
 * Input: a 4x2 RGB frame with differently colored halves; person ROI (0,0,2,2) at 0.9 and vehicle ROI (2,0,2,2) at 0.8.
 * Values: target-classes=person,vehicle, threshold=0.5, output=4x4; input PTS/DTS/duration/offset=123/100/40/7.
 * Expected: exactly two crops in person/vehicle order, each with its source color, full-output ROI metadata,
 *           original confidence and timing/offset values, and no extra outputs.
 */
TEST_F(GvaRoiScaleTest, MultipleClassesProduceSeparateCrops) {
    GstBuffer *input = make_frame();
    add_detection(input, "person", 0, 0, 2, 2, 0.9f);
    add_detection(input, "vehicle", 2, 0, 2, 2, 0.8f);
    ASSERT_EQ(gst_harness_push(harness, input), GST_FLOW_OK);
    ASSERT_EQ(gst_harness_buffers_in_queue(harness), 2u);
    for (guint index = 0; index < 2; ++index) {
        BufferPtr output(gst_harness_try_pull(harness), gst_buffer_unref);
        ASSERT_NE(output, nullptr);
        expect_detection(output.get(), index == 0 ? "person" : "vehicle", index == 0 ? 0.9f : 0.8f, 0, 0, 4, 4);
        EXPECT_EQ(GST_BUFFER_PTS(output.get()), 123u);
        EXPECT_EQ(GST_BUFFER_DTS(output.get()), 100u);
        EXPECT_EQ(GST_BUFFER_DURATION(output.get()), 40u);
        EXPECT_EQ(GST_BUFFER_OFFSET(output.get()), 7u);
        std::array<guint8, 48> pixels{};
        ASSERT_EQ(gst_buffer_extract(output.get(), 0, pixels.data(), pixels.size()), pixels.size());
        EXPECT_EQ(gst_buffer_get_size(output.get()), pixels.size());
        for (guint pixel = 0; pixel < 16; ++pixel) {
            EXPECT_EQ(pixels[pixel * 3], index == 0 ? 10 : 100);
            EXPECT_EQ(pixels[pixel * 3 + 1], index == 0 ? 20 : 110);
            EXPECT_EQ(pixels[pixel * 3 + 2], index == 0 ? 30 : 120);
        }
    }
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
}

/* TEST
 * Tests negotiated output caps, aspect-ratio preservation, letterbox pixels, and output ROI coordinates.
 * Input: a 4x2 RGB frame with differently colored halves and a full-frame person ROI at 0.9.
 * Values: requested output=4x4 RGB at 30/1 fps; source aspect ratio=2:1, output aspect ratio=1:1.
 * Expected: exactly one 4x4 RGB output, black top/bottom rows, unchanged source pixels in the middle rows,
 *           and person detection metadata at (0,1,4,2) with confidence=0.9.
 */
TEST_F(GvaRoiScaleTest, NegotiatesOutputAndPreservesLetterboxPixels) {
    GstBuffer *input = make_frame();
    add_detection(input, "person", 0, 0, 4, 2, 0.9f);
    ASSERT_EQ(gst_harness_push(harness, input), GST_FLOW_OK);
    ASSERT_EQ(gst_harness_buffers_in_queue(harness), 1u);
    BufferPtr output(gst_harness_try_pull(harness), gst_buffer_unref);
    ASSERT_NE(output, nullptr);
    GstCaps *caps = gst_pad_get_current_caps(harness->sinkpad);
    ASSERT_NE(caps, nullptr);
    GstVideoInfo info;
    const gboolean parsed = gst_video_info_from_caps(&info, caps);
    gst_caps_unref(caps);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(GST_VIDEO_INFO_FORMAT(&info), GST_VIDEO_FORMAT_RGB);
    EXPECT_EQ(GST_VIDEO_INFO_WIDTH(&info), 4u);
    EXPECT_EQ(GST_VIDEO_INFO_HEIGHT(&info), 4u);
    expect_detection(output.get(), "person", 0.9f, 0, 1, 4, 2);
    std::array<guint8, 48> pixels{};
    ASSERT_EQ(gst_buffer_extract(output.get(), 0, pixels.data(), pixels.size()), pixels.size());
    EXPECT_EQ(gst_buffer_get_size(output.get()), pixels.size());
    for (guint row = 0; row < 4; ++row) {
        for (guint column = 0; column < 4; ++column) {
            for (guint channel = 0; channel < 3; ++channel) {
                const guint8 expected = row == 0 || row == 3 ? 0 : (column < 2 ? 10 : 100) + channel * 10;
                EXPECT_EQ(pixels[row * 12 + column * 3 + channel], expected);
            }
        }
    }
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
}

/* TEST
 * Tests propagation of discontinuity when one input frame produces multiple ROI crops.
 * Input: one 4x2 RGB frame with DISCONT, person ROI (0,0,2,2) at 0.9 and vehicle ROI (2,0,2,2) at 0.8.
 * Values: target-classes=person,vehicle, threshold=0.5, output=4x4; input PTS=123.
 * Expected: exactly two outputs with PTS=123; DISCONT is set only on the first output.
 */
TEST_F(GvaRoiScaleTest, DiscontAppearsOnlyOnFirstCrop) {
    GstBuffer *input = make_frame();
    GST_BUFFER_FLAG_SET(input, GST_BUFFER_FLAG_DISCONT);
    add_detection(input, "person", 0, 0, 2, 2, 0.9f);
    add_detection(input, "vehicle", 2, 0, 2, 2, 0.8f);
    ASSERT_EQ(gst_harness_push(harness, input), GST_FLOW_OK);
    ASSERT_EQ(gst_harness_buffers_in_queue(harness), 2u);
    for (guint index = 0; index < 2; ++index) {
        BufferPtr output(gst_harness_try_pull(harness), gst_buffer_unref);
        ASSERT_NE(output, nullptr);
        EXPECT_EQ(static_cast<bool>(GST_BUFFER_FLAG_IS_SET(output.get(), GST_BUFFER_FLAG_DISCONT)), index == 0);
        EXPECT_EQ(GST_BUFFER_PTS(output.get()), 123u);
    }
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
}

} // namespace

/* TEST
 * Tests class/confidence filtering, clipping to frame bounds, and keeping one ROI per class.
 * Input: 6x8 frame with two person detections, one vehicle, and one low-confidence person detection.
 * Values: threshold=0.5; person boxes start at x=-2/-1; confidences are 0.95, 0.90, 0.85, and 0.40.
 * Expected: two ROIs; the 0.95 person ROI is clipped to (0,1,6,7), the vehicle is retained,
 *           and duplicate/low-score persons are omitted.
 */
TEST(RoiScale, CollectsClippedTargetsByConfidenceAndSuppressesSameClassDuplicates) {
    GstBuffer *buffer = gst_buffer_new();
    add_detection(buffer, "person", -2, 1, 8, 8, 0.95f);
    add_detection(buffer, "person", -1, 1, 8, 8, 0.90f);
    add_detection(buffer, "vehicle", 0, 1, 6, 8, 0.85f);
    add_detection(buffer, "person", 2, 2, 2, 2, 0.40f);
    GArray *classes = make_classes({"person", "vehicle"});

    const auto rois = gvaroiscale::collect_rois(buffer, classes, 6, 8, 0.5f);

    ASSERT_EQ(rois.size(), 2u);
    EXPECT_EQ(g_quark_to_string(rois[0].label), std::string("person"));
    EXPECT_FLOAT_EQ(rois[0].confidence, 0.95f);
    EXPECT_EQ(rois[0].x, 0);
    EXPECT_EQ(rois[0].y, 1);
    EXPECT_EQ(rois[0].width, 6);
    EXPECT_EQ(rois[0].height, 7);
    EXPECT_EQ(g_quark_to_string(rois[1].label), std::string("vehicle"));

    g_array_unref(classes);
    gst_buffer_unref(buffer);
}

/* TEST
 * Tests that aggregate ROI metadata takes precedence over object-detection metadata.
 * Input: person detection (0,0,2,2) at 0.9 and aggregate ROI (2,1,3,4); target class is person.
 * Values: frame size=8x8; threshold=0.9, equal to the detection confidence.
 * Expected: one ROI labelled as the aggregate type, with location (2,1,3,4).
 */
TEST(RoiScale, AggregatedRoiTakesPrecedenceOverDetectionMetadata) {
    GstBuffer *buffer = gst_buffer_new();
    GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(buffer);
    GstAnalyticsODMtd detection;
    ASSERT_TRUE(gst_analytics_relation_meta_add_od_mtd(relation_meta, g_quark_from_static_string("person"), 0, 0, 2,
                                                       2, 0.9f, &detection));
    ASSERT_NE(gst_buffer_add_video_region_of_interest_meta(buffer, GVA_AGGREGATED_ROI_TYPE, 2, 1, 3, 4), nullptr);
    GArray *classes = make_classes({"person"});

    const auto rois = gvaroiscale::collect_rois(buffer, classes, 8, 8, 0.9f);

    ASSERT_EQ(rois.size(), 1u);
    EXPECT_EQ(rois[0].label, g_quark_from_static_string(GVA_AGGREGATED_ROI_TYPE));
    EXPECT_EQ(rois[0].x, 2);
    EXPECT_EQ(rois[0].y, 1);
    EXPECT_EQ(rois[0].width, 3);
    EXPECT_EQ(rois[0].height, 4);

    g_array_unref(classes);
    gst_buffer_unref(buffer);
}

/* TEST
 * Tests aspect-ratio-preserving scaling, letterboxing, and frame timing/offset metadata preservation.
 * Input: 4x2 RGB image scaled to 4x4 using a full-frame ROI; input PTS/DTS/duration/offset are 123/100/40/7.
 * Values: source aspect ratio=2:1, output aspect ratio=1:1.
 * Expected: content rectangle (0,1,4,2), black top/bottom rows, unchanged source pixels and timing/offset metadata.
 */
TEST(RoiScale, PreservesAspectRatioAndCentersCropOnBlackCanvas) {
    GstVideoInfo input_info;
    ASSERT_TRUE(gst_video_info_set_format(&input_info, GST_VIDEO_FORMAT_RGB, 4, 2));
    const std::vector<guint8> pixels = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                        21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
    GstBuffer *input = make_rgb_buffer(input_info, pixels);
    GST_BUFFER_PTS(input) = 123;
    GST_BUFFER_DTS(input) = 100;
    GST_BUFFER_DURATION(input) = 40;
    GST_BUFFER_OFFSET(input) = 7;
    GstBuffer *output = nullptr;
    GstVideoInfo output_info;
    GstVideoRectangle content_rect;
    const gvaroiscale::Roi roi{g_quark_from_static_string("person"), 0.9f, 0, 0, 4, 2};

    ASSERT_EQ(gvaroiscale::roi_scale_buffer(input, input_info, roi, 4, 4, &output, &output_info, &content_rect),
              gvaroiscale::RoiScaleResult::Ok);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(GST_VIDEO_INFO_WIDTH(&output_info), 4u);
    EXPECT_EQ(GST_VIDEO_INFO_HEIGHT(&output_info), 4u);
    EXPECT_EQ(content_rect.x, 0);
    EXPECT_EQ(content_rect.y, 1);
    EXPECT_EQ(content_rect.w, 4);
    EXPECT_EQ(content_rect.h, 2);
    EXPECT_EQ(GST_BUFFER_PTS(output), 123u);
    EXPECT_EQ(GST_BUFFER_DTS(output), 100u);
    EXPECT_EQ(GST_BUFFER_DURATION(output), 40u);
    EXPECT_EQ(GST_BUFFER_OFFSET(output), 7u);

    GstMapInfo map = GST_MAP_INFO_INIT;
    ASSERT_TRUE(gst_buffer_map(output, &map, GST_MAP_READ));
    const guint8 *bytes = map.data;
    const gsize stride = GST_VIDEO_INFO_PLANE_STRIDE(&output_info, 0);
    for (gsize i = 0; i < 4 * 3; ++i) {
        EXPECT_EQ(bytes[i], 0u);
        EXPECT_EQ(bytes[3 * stride + i], 0u);
    }
    for (gsize i = 0; i < pixels.size(); ++i) {
        const gsize row = i / 12;
        const gsize column = i % 12;
        EXPECT_EQ(bytes[stride + row * stride + column], pixels[i]);
    }
    gst_buffer_unmap(output, &map);
    gst_buffer_unref(output);
    gst_buffer_unref(input);
}

/* TEST
 * Tests rejection of an ROI wholly outside the source frame.
 * Input: 4x2 RGB frame and ROI at (8,8,2,2), requested output 4x4.
 * Values: ROI origin is beyond both source dimensions.
 * Expected: RoiScaleResult::NoRoi and a null output buffer.
 */
TEST(RoiScale, ReturnsNoRoiForCropOutsideInput) {
    GstVideoInfo input_info;
    ASSERT_TRUE(gst_video_info_set_format(&input_info, GST_VIDEO_FORMAT_RGB, 4, 2));
    GstBuffer *input = gst_buffer_new_allocate(nullptr, input_info.size, nullptr);
    GstBuffer *output = nullptr;
    const gvaroiscale::Roi roi{g_quark_from_static_string("person"), 0.9f, 8, 8, 2, 2};

    EXPECT_EQ(gvaroiscale::roi_scale_buffer(input, input_info, roi, 4, 4, &output, nullptr),
              gvaroiscale::RoiScaleResult::NoRoi);
    EXPECT_EQ(output, nullptr);

    gst_buffer_unref(input);
}

int main(int argc, char **argv) {
    // Initialize GStreamer before tests use its API, then prepare GoogleTest before running them.
    gst_init(&argc, &argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}