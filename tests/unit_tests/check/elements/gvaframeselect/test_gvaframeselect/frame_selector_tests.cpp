/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* TEST
 * Overview: Tests frame index selection and element-level buffer selection, EOS draining, flush handling,
 *           and aggregated ROI metadata.
 */

#include "frame_selector.hpp"
#include "gstgvaframeselect.h"
#include "gva_utils.h"

#include <gst/analytics/analytics.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>
#include <gtest/gtest.h>

#include <memory>

namespace {

using BufferPtr = std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)>;

class GvaFrameSelectTest : public ::testing::Test {
  protected:
    void SetUp() override {
        GstElement *element = GST_ELEMENT(g_object_new(GST_TYPE_GVA_FRAME_SELECT, "sub-stream-frames", 10u,
                                                       "frame-budget", 3u, "roi-padding", 2u, nullptr));
        harness = gst_harness_new_with_element(element, "sink", "src");
        gst_object_unref(element);
        ASSERT_NE(harness, nullptr);
        gst_harness_set_src_caps_str(harness, "video/x-raw,format=BGR,width=16,height=16,framerate=30/1");
    }

    void TearDown() override {
        if (harness)
            gst_harness_teardown(harness);
    }

    GstBuffer *make_frame(guint index) {
        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, 16 * 16 * 3, nullptr);
        gst_buffer_memset(buffer, 0, index, gst_buffer_get_size(buffer));
        GST_BUFFER_PTS(buffer) = index * GST_SECOND / 30;
        return buffer;
    }

    void add_detection(GstBuffer *buffer, const char *label, gint x, gint y, gint width, gint height,
                       gfloat confidence) {
        GstAnalyticsRelationMeta *meta = gst_buffer_get_analytics_relation_meta(buffer);
        if (!meta)
            meta = gst_buffer_add_analytics_relation_meta(buffer);
        ASSERT_NE(meta, nullptr);
        GstAnalyticsODMtd detection;
        ASSERT_TRUE(gst_analytics_relation_meta_add_od_mtd(meta, g_quark_from_string(label), x, y, width, height,
                                                           confidence, &detection));
    }

    GstHarness *harness = nullptr;
};

/* TEST
 * Tests selected output buffers across consecutive blocks, including frame identity and timing.
 * Input: twenty 16x16 BGR frames with distinct pixel values and PTS derived from their input indices.
 * Values: sub-stream-frames=10, frame-budget=3, framerate=30/1.
 * Expected: exactly six outputs with offsets {0,5,9,10,15,19}, matching PTS and pixel values, and no extras.
 */
TEST_F(GvaFrameSelectTest, SelectsExpectedBuffersAcrossTwoBlocks) {
    for (guint index = 0; index < 20; ++index)
        ASSERT_EQ(gst_harness_push(harness, make_frame(index)), GST_FLOW_OK);

    ASSERT_EQ(gst_harness_buffers_in_queue(harness), 6u);
    for (guint index : {0u, 5u, 9u, 10u, 15u, 19u}) {
        BufferPtr output(gst_harness_try_pull(harness), gst_buffer_unref);
        ASSERT_NE(output, nullptr);
        EXPECT_EQ(GST_BUFFER_OFFSET(output.get()), index);
        EXPECT_EQ(GST_BUFFER_PTS(output.get()), index * GST_SECOND / 30);
        guint8 pixel = 0;
        ASSERT_EQ(gst_buffer_extract(output.get(), 0, &pixel, 1), 1u);
        EXPECT_EQ(pixel, index);
    }
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
}

/* TEST
 * Tests that EOS forwards selected frames buffered in an incomplete block.
 * Input: frame 0 followed by EOS, without completing the ten-frame block.
 * Values: sub-stream-frames=10, frame-budget=3.
 * Expected: no output before EOS; exactly one output afterwards, with offset=0 and PTS=0.
 */
TEST_F(GvaFrameSelectTest, EosDrainsPartialBlock) {
    ASSERT_EQ(gst_harness_push(harness, make_frame(0)), GST_FLOW_OK);
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
    ASSERT_TRUE(gst_harness_push_event(harness, gst_event_new_eos()));
    ASSERT_EQ(gst_harness_buffers_in_queue(harness), 1u);
    BufferPtr output(gst_harness_try_pull(harness), gst_buffer_unref);
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(GST_BUFFER_OFFSET(output.get()), 0u);
    EXPECT_EQ(GST_BUFFER_PTS(output.get()), 0u);
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
}

/* TEST
 * Tests that flushing discards selected frames waiting for block completion.
 * Input: frame 0, FLUSH_START, FLUSH_STOP with reset_time=TRUE, then EOS.
 * Values: sub-stream-frames=10, frame-budget=3; the first selected frame remains pending before flush.
 * Expected: no output before flush or after EOS; the discarded frame is never forwarded.
 */
TEST_F(GvaFrameSelectTest, FlushDiscardsPendingFrames) {
    ASSERT_EQ(gst_harness_push(harness, make_frame(0)), GST_FLOW_OK);
    ASSERT_EQ(gst_harness_buffers_in_queue(harness), 0u);
    ASSERT_TRUE(gst_harness_push_event(harness, gst_event_new_flush_start()));
    ASSERT_TRUE(gst_harness_push_event(harness, gst_event_new_flush_stop(TRUE)));
    ASSERT_TRUE(gst_harness_push_event(harness, gst_event_new_eos()));
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
}

/* TEST
 * Tests ROI aggregation over selected frames, class/confidence filtering, padding, and clipping.
 * Input: 16x16 frames with person ROI (1,3,3,4) at 0.9 on frame 0 and vehicle ROI (12,8,8,3) at 0.8 on frame 2.
 *        Distractors include low-confidence/wrong-class full-frame detections and one on unselected frame 1.
 * Values: sub-stream-frames=3, frame-budget=2, roi-padding=2; default targets person,vehicle and threshold=0.5.
 * Expected: only frames 0 and 2 are output; both carry aggregate ROI (0,1,16,12), with no extra outputs.
 */
TEST_F(GvaFrameSelectTest, AggregatesAndClipsRoiAcrossSelectedFrames) {
    g_object_set(harness->element, "sub-stream-frames", 3u, "frame-budget", 2u, nullptr);
    GstBuffer *first = make_frame(0);
    add_detection(first, "person", 1, 3, 3, 4, 0.9f);
    add_detection(first, "person", 0, 0, 16, 16, 0.1f);
    add_detection(first, "cat", 0, 0, 16, 16, 0.99f);
    ASSERT_EQ(gst_harness_push(harness, first), GST_FLOW_OK);
    GstBuffer *unselected = make_frame(1);
    add_detection(unselected, "person", 0, 0, 16, 16, 0.99f);
    ASSERT_EQ(gst_harness_push(harness, unselected), GST_FLOW_OK);
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
    GstBuffer *last = make_frame(2);
    add_detection(last, "vehicle", 12, 8, 8, 3, 0.8f);
    ASSERT_EQ(gst_harness_push(harness, last), GST_FLOW_OK);
    ASSERT_EQ(gst_harness_buffers_in_queue(harness), 2u);
    for (guint index : {0u, 2u}) {
        BufferPtr output(gst_harness_try_pull(harness), gst_buffer_unref);
        ASSERT_NE(output, nullptr);
        EXPECT_EQ(GST_BUFFER_OFFSET(output.get()), index);
        GstVideoRegionOfInterestMeta *roi = gst_buffer_get_video_region_of_interest_meta(output.get());
        ASSERT_NE(roi, nullptr);
        EXPECT_EQ(roi->roi_type, g_quark_from_static_string(GVA_AGGREGATED_ROI_TYPE));
        EXPECT_EQ(roi->x, 0u);
        EXPECT_EQ(roi->y, 1u);
        EXPECT_EQ(roi->w, 16u);
        EXPECT_EQ(roi->h, 12u);
    }
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
}

} // namespace

/* TEST
 * Tests empty frame input and zero selection budget.
 * Input: select_indices(0,3) and select_indices(10,0).
 * Values: frame count=0 and budget=0 are the empty-selection boundaries.
 * Expected: both calls return an empty index list.
 */
TEST(FrameSelector, EmptyInputOrBudgetSelectsNothing) {
    EXPECT_TRUE(gvaframeselect::select_indices(0, 3).empty());
    EXPECT_TRUE(gvaframeselect::select_indices(10, 0).empty());
}

/* TEST
 * Tests selection of a single frame from a non-empty block.
 * Input: select_indices(10,1).
 * Values: block size=10, budget=1.
 * Expected: only index 0 is selected.
 */
TEST(FrameSelector, SingleBudgetSelectsFirstFrame) {
    EXPECT_EQ(gvaframeselect::select_indices(10, 1), (std::vector<std::size_t>{0}));
}

/* TEST
 * Tests even spacing of a three-frame budget across a ten-frame block.
 * Input: select_indices(10,3).
 * Values: selected positions must include both block endpoints.
 * Expected: indices {0,5,9}.
 */
TEST(FrameSelector, SpreadsBudgetAcrossBlockIncludingEndpoints) {
    EXPECT_EQ(gvaframeselect::select_indices(10, 3), (std::vector<std::size_t>{0, 5, 9}));
}

/* TEST
 * Tests a selection budget larger than the input block.
 * Input: select_indices(3,8).
 * Values: budget=8 exceeds frame count=3.
 * Expected: every available index is returned once: {0,1,2}.
 */
TEST(FrameSelector, BudgetAboveFrameCountSelectsEveryFrame) {
    EXPECT_EQ(gvaframeselect::select_indices(3, 8), (std::vector<std::size_t>{0, 1, 2}));
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}