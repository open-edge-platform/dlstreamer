/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* TEST
 * Overview: Tests event-session filtering, cooldown and overflow limits, PRE-ring eviction, and drop-log throttling.
 *           Also tests element-level filtering, PRE/POST forwarding, event metadata, and overlapping sessions.
 */

#include "drop_log_throttle/drop_log_throttle.hpp"
#include "event_session_controller/event_session_controller.hpp"
#include "gstgvaeventrecorder.h"
#include "pre_event_ring/pre_event_ring.hpp"

#include <glib/gstdio.h>
#include <gst/analytics/analytics.h>
#include <gst/check/gstharness.h>
#include <gst/gst.h>
#include <gtest/gtest.h>

#include <memory>

using namespace gvaeventrecorder;

namespace {

using BufferPtr = std::unique_ptr<GstBuffer, decltype(&gst_buffer_unref)>;

bool factory_available(const char *name) {
    GstElementFactory *factory = gst_element_factory_find(name);
    if (!factory)
        return false;
    gst_object_unref(factory);
    return true;
}

class GvaEventRecorderTest : public ::testing::Test {
  protected:
    void SetUp() override {
        GError *error = nullptr;
        output_dir = g_dir_make_tmp("gva-recorder-harness-XXXXXX", &error);
        const std::string message = error ? error->message : "Could not create temporary directory";
        g_clear_error(&error);
        ASSERT_NE(output_dir, nullptr) << message;
        GstElement *element = GST_ELEMENT(
            g_object_new(GST_TYPE_GVAEVENTRECORDER, "target-classes", "person,vehicle", "confidence-threshold", 0.5,
                         "pre-event-frames", 2u, "post-event-frames", 2u, "event-gap-frames", 0u, "cooldown-value",
                         static_cast<guint64>(0), "encoder-mode", GST_GVAEVENTRECORDER_ENCODER_SOFTWARE,
                         "event-clip-output-dir", output_dir, "flush-interval-ms", 0u, nullptr));
        harness = gst_harness_new_with_element(element, "sink", "src");
        gst_object_unref(element);
        ASSERT_NE(harness, nullptr);
        gst_harness_set_src_caps_str(harness, "video/x-raw,format=BGR,width=64,height=64,framerate=30/1");
    }

    void TearDown() override {
        if (harness)
            gst_harness_teardown(harness);
        if (output_dir) {
            GDir *directory = g_dir_open(output_dir, 0, nullptr);
            if (directory) {
                while (const gchar *name = g_dir_read_name(directory)) {
                    gchar *path = g_build_filename(output_dir, name, nullptr);
                    EXPECT_EQ(g_unlink(path), 0) << path;
                    g_free(path);
                }
                g_dir_close(directory);
            }
            EXPECT_EQ(g_rmdir(output_dir), 0);
            g_free(output_dir);
        }
    }

    bool recording_plugins_available() {
        return (factory_available("x264enc") || factory_available("openh264enc")) && factory_available("h264parse") &&
               factory_available("mp4mux") && factory_available("videoconvert");
    }

    GstBuffer *make_frame(guint index, const char *label = nullptr, gfloat confidence = 0.9f) {
        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, 64 * 64 * 3, nullptr);
        gst_buffer_memset(buffer, 0, index, gst_buffer_get_size(buffer));
        GST_BUFFER_PTS(buffer) = index * GST_SECOND / 30;
        GST_BUFFER_DTS(buffer) = GST_BUFFER_PTS(buffer);
        GST_BUFFER_DURATION(buffer) = GST_SECOND / 30;
        if (label) {
            GstAnalyticsRelationMeta *meta = gst_buffer_add_analytics_relation_meta(buffer);
            GstAnalyticsODMtd detection;
            if (!meta || !gst_analytics_relation_meta_add_od_mtd(meta, g_quark_from_string(label), 16, 16, 32, 32,
                                                                 confidence, &detection))
                ADD_FAILURE() << "Could not attach synthetic detection";
        }
        return buffer;
    }

    void expect_frame(GstBuffer *buffer, guint index) {
        EXPECT_EQ(GST_BUFFER_PTS(buffer), index * GST_SECOND / 30);
        EXPECT_EQ(GST_BUFFER_DTS(buffer), index * GST_SECOND / 30);
        EXPECT_EQ(GST_BUFFER_DURATION(buffer), GST_SECOND / 30);
        guint8 pixel = 0;
        ASSERT_EQ(gst_buffer_extract(buffer, 0, &pixel, 1), 1u);
        EXPECT_EQ(pixel, index);
    }

    void expect_event_metadata(GstBuffer *buffer, const char *event_state, const char *trigger_class = nullptr,
                               gfloat confidence = 0.9f) {
        GstAnalyticsRelationMeta *meta = gst_buffer_get_analytics_relation_meta(buffer);
        ASSERT_NE(meta, nullptr);
        gpointer state = nullptr;
        GstAnalyticsClsMtd classification;
        guint event_count = 0, trigger_count = 0;
        while (
            gst_analytics_relation_meta_iterate(meta, &state, gst_analytics_cls_mtd_get_mtd_type(), &classification)) {
            if (gst_analytics_mtd_has_semantic_tag(reinterpret_cast<GstAnalyticsMtd *>(&classification), "event")) {
                ++event_count;
                ASSERT_EQ(gst_analytics_cls_mtd_get_length(&classification), 1u);
                EXPECT_EQ(gst_analytics_cls_mtd_get_quark(&classification, 0), g_quark_from_string(event_state));
                EXPECT_FLOAT_EQ(gst_analytics_cls_mtd_get_level(&classification, 0), 1.0f);
            }
            if (gst_analytics_mtd_has_semantic_tag(reinterpret_cast<GstAnalyticsMtd *>(&classification),
                                                   "event_trigger")) {
                ++trigger_count;
                ASSERT_NE(trigger_class, nullptr);
                ASSERT_EQ(gst_analytics_cls_mtd_get_length(&classification), 1u);
                EXPECT_EQ(gst_analytics_cls_mtd_get_quark(&classification, 0), g_quark_from_string(trigger_class));
                EXPECT_FLOAT_EQ(gst_analytics_cls_mtd_get_level(&classification, 0), confidence);
            }
        }
        EXPECT_EQ(event_count, 1u);
        EXPECT_EQ(trigger_count, trigger_class ? 1u : 0u);
    }

    GstHarness *harness = nullptr;
    gchar *output_dir = nullptr;
};

/* TEST
 * Tests that absent, wrong-class, and low-confidence detections do not trigger forwarding or clip creation.
 * Input: three 64x64 BGR frames: no detections, cat at 0.9, and person at 0.4; then EOS.
 * Values: target-classes=person,vehicle, confidence-threshold=0.5, PRE/POST=2/2, event-gap=0, cooldown=0.
 * Expected: no output buffers, including after EOS, and an empty temporary clip directory.
 */
TEST_F(GvaEventRecorderTest, NoMatchingDetectionProducesNoOutput) {
    ASSERT_EQ(gst_harness_push(harness, make_frame(0)), GST_FLOW_OK);
    ASSERT_EQ(gst_harness_push(harness, make_frame(1, "cat")), GST_FLOW_OK);
    ASSERT_EQ(gst_harness_push(harness, make_frame(2, "person", 0.4f)), GST_FLOW_OK);
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
    ASSERT_TRUE(gst_harness_push_event(harness, gst_event_new_eos()));
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
    GDir *directory = g_dir_open(output_dir, 0, nullptr);
    ASSERT_NE(directory, nullptr);
    EXPECT_EQ(g_dir_read_name(directory), nullptr);
    g_dir_close(directory);
}

/* TEST
 * Tests ordered PRE, trigger, and POST forwarding without retaining older frames or duplicating the trigger.
 * Input: frames 0-6 with distinct pixels and timestamps; only frame 3 contains a person detection at 0.9; then EOS.
 * Values: PRE/POST=2/2, event-gap=0, cooldown=0, threshold=0.5; clips use a temporary directory.
 * Expected: no output before the trigger, three outputs at the trigger, then exactly frames 1-5 in order;
 *           pixels and PTS/DTS/duration are preserved, and only frame 1 has DISCONT.
 * Requires: software H.264 encoder, h264parse, mp4mux, and videoconvert; skipped if unavailable.
 */
TEST_F(GvaEventRecorderTest, ForwardsPreTriggerAndPostFramesInOrder) {
    if (!recording_plugins_available())
        GTEST_SKIP() << "Software H.264 encoder, h264parse, mp4mux and videoconvert are required";
    for (guint index = 0; index < 3; ++index)
        ASSERT_EQ(gst_harness_push(harness, make_frame(index)), GST_FLOW_OK);
    ASSERT_EQ(gst_harness_buffers_in_queue(harness), 0u);
    ASSERT_EQ(gst_harness_push(harness, make_frame(3, "person")), GST_FLOW_OK);
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 3u);
    for (guint index = 4; index < 7; ++index)
        ASSERT_EQ(gst_harness_push(harness, make_frame(index)), GST_FLOW_OK);
    ASSERT_TRUE(gst_harness_push_event(harness, gst_event_new_eos()));
    ASSERT_EQ(gst_harness_buffers_in_queue(harness), 5u);
    for (guint index = 1; index <= 5; ++index) {
        BufferPtr output(gst_harness_try_pull(harness), gst_buffer_unref);
        ASSERT_NE(output, nullptr);
        expect_frame(output.get(), index);
        EXPECT_EQ(static_cast<bool>(GST_BUFFER_FLAG_IS_SET(output.get(), GST_BUFFER_FLAG_DISCONT)), index == 1);
    }
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
}

/* TEST
 * Tests semantic event-state and trigger metadata across an event with PRE and POST frames.
 * Input: frames 0-4; only frame 2 contains a person detection at confidence=0.85.
 * Values: PRE/POST=2/2, event-gap=0, cooldown=0, threshold=0.5; clips use a temporary directory.
 * Expected: exactly frames 0-4 with event states start/continue/continue/continue/stop at confidence=1.0;
 *           event_trigger=person at 0.85 appears only on frame 2, and DISCONT only on frame 0.
 * Requires: software H.264 encoder, h264parse, mp4mux, and videoconvert; skipped if unavailable.
 */
TEST_F(GvaEventRecorderTest, AddsEventStateAndTriggerMetadata) {
    if (!recording_plugins_available())
        GTEST_SKIP() << "Software H.264 encoder, h264parse, mp4mux and videoconvert are required";
    for (guint index = 0; index < 5; ++index)
        ASSERT_EQ(gst_harness_push(harness, make_frame(index, index == 2 ? "person" : nullptr, 0.85f)), GST_FLOW_OK);
    ASSERT_EQ(gst_harness_buffers_in_queue(harness), 5u);
    for (guint index = 0; index < 5; ++index) {
        BufferPtr output(gst_harness_try_pull(harness), gst_buffer_unref);
        ASSERT_NE(output, nullptr);
        expect_frame(output.get(), index);
        const char *event_state = index == 0 ? "start" : (index == 4 ? "stop" : "continue");
        expect_event_metadata(output.get(), event_state, index == 2 ? "person" : nullptr, 0.85f);
        EXPECT_EQ(static_cast<bool>(GST_BUFFER_FLAG_IS_SET(output.get(), GST_BUFFER_FLAG_DISCONT)), index == 0);
    }
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
}

/* TEST
 * Tests that overlapping class-specific sessions share downstream frames without replaying PRE buffers.
 * Input: frame 0 without detections, person at 0.9 on frame 1, vehicle at 0.8 on frame 2, quiet frames 3-5, then EOS.
 * Values: target-classes=person,vehicle, PRE/POST=2/2, event-gap=0, cooldown=0; clips use a temporary directory.
 * Expected: exactly frames 0-4 once each, start on frame 0, stop on frame 4, and continue in between;
 *           trigger metadata only on frames 1/2 for person/vehicle respectively, and DISCONT only on frame 0.
 * Requires: software H.264 encoder, h264parse, mp4mux, and videoconvert; skipped if unavailable.
 */
TEST_F(GvaEventRecorderTest, OverlappingSessionsDoNotDuplicateForwardedFrames) {
    if (!recording_plugins_available())
        GTEST_SKIP() << "Software H.264 encoder, h264parse, mp4mux and videoconvert are required";
    ASSERT_EQ(gst_harness_push(harness, make_frame(0)), GST_FLOW_OK);
    ASSERT_EQ(gst_harness_push(harness, make_frame(1, "person")), GST_FLOW_OK);
    ASSERT_EQ(gst_harness_push(harness, make_frame(2, "vehicle", 0.8f)), GST_FLOW_OK);
    for (guint index = 3; index <= 5; ++index)
        ASSERT_EQ(gst_harness_push(harness, make_frame(index)), GST_FLOW_OK);
    ASSERT_EQ(gst_harness_buffers_in_queue(harness), 5u);
    for (guint index = 0; index < 5; ++index) {
        BufferPtr output(gst_harness_try_pull(harness), gst_buffer_unref);
        ASSERT_NE(output, nullptr);
        expect_frame(output.get(), index);
        const char *event_state = index == 0 ? "start" : (index == 4 ? "stop" : "continue");
        const char *trigger = index == 1 ? "person" : (index == 2 ? "vehicle" : nullptr);
        expect_event_metadata(output.get(), event_state, trigger, index == 2 ? 0.8f : 0.9f);
        EXPECT_EQ(static_cast<bool>(GST_BUFFER_FLAG_IS_SET(output.get(), GST_BUFFER_FLAG_DISCONT)), index == 0);
    }
    ASSERT_TRUE(gst_harness_push_event(harness, gst_event_new_eos()));
    EXPECT_EQ(gst_harness_buffers_in_queue(harness), 0u);
}

} // namespace

/* TEST
 * Tests class/confidence filtering, session completion, and cooldown.
 * Input: calls at ticks 10, 11, 12, 14, and 15 with car/person detections at confidences
 *        0.99, 0.59, 0.90, 0.95, and 0.80.
 * Values: target=person, threshold=0.60, cooldown=5 frames, event gap=0, POST=0.
 * Expected: only qualifying person detections trigger; tick 12 finishes the session, tick 14 is suppressed,
 *           and tick 15 triggers again.
 */
TEST(EventSessionController, AppliesClassConfidenceAndCooldownFilters) {
    EventSessionController::Config config;
    config.target_classes = {"person"};
    config.confidence_threshold = 0.6f;
    config.cooldown_value = 5;
    config.event_gap_frames = 0;
    config.post_event_frames = 0;
    EventSessionController controller(config);

    auto outcome = controller.process_frame(10, {{"car", 0.99f}, {"person", 0.59f}, {"person", 0.9f}});
    ASSERT_EQ(outcome.new_triggers.size(), 1u);
    EXPECT_EQ(outcome.new_triggers[0].class_label, "person");
    EXPECT_FLOAT_EQ(outcome.new_triggers[0].confidence, 0.9f);
    EXPECT_TRUE(outcome.forward);

    outcome = controller.process_frame(11, {{"person", 0.95f}});
    EXPECT_TRUE(outcome.new_triggers.empty());

    outcome = controller.process_frame(12, {});
    ASSERT_EQ(outcome.finished_sessions.size(), 1u);
    EXPECT_FALSE(outcome.forward);

    EXPECT_TRUE(controller.process_frame(14, {{"person", 0.8f}}).new_triggers.empty());
    EXPECT_EQ(controller.process_frame(15, {{"person", 0.8f}}).new_triggers.size(), 1u);
}

/* TEST
 * Tests the maximum number of concurrent event sessions and overflow counting.
 * Input: person at 0.90 and vehicle at 0.80 on tick 1; both classes are targets.
 * Values: max-concurrent-events=1.
 * Expected: person starts one session and the other trigger increments dropped_by_overflow to 1.
 */
TEST(EventSessionController, LimitsConcurrentSessionsAndCountsDroppedTriggers) {
    EventSessionController::Config config;
    config.target_classes = {"person", "vehicle"};
    config.max_concurrent_events = 1;
    EventSessionController controller(config);

    const auto outcome = controller.process_frame(1, {{"person", 0.9f}, {"vehicle", 0.8f}});

    ASSERT_EQ(outcome.new_triggers.size(), 1u);
    EXPECT_EQ(outcome.new_triggers[0].class_label, "person");
    EXPECT_EQ(outcome.dropped_by_overflow, 1u);
}

/* TEST
 * Tests ring-buffer eviction when pushing past capacity and when shrinking the capacity.
 * Input: capacity 3, push 1-4, then set capacity to 1.
 * Values: initial capacity=3; resized capacity=1.
 * Expected: snapshots are {2,3,4} then {4}; eviction callback receives {1,2,3} in order.
 */
TEST(PreEventRing, KeepsNewestFramesAndEvictsWhenCapacityShrinks) {
    std::vector<int> evicted;
    PreEventRing<int> ring(3, [&evicted](int &value) { evicted.push_back(value); });
    ring.push(1);
    ring.push(2);
    ring.push(3);
    ring.push(4);
    EXPECT_EQ(ring.snapshot(), (std::vector<int>{2, 3, 4}));

    ring.set_capacity(1);
    EXPECT_EQ(ring.snapshot(), (std::vector<int>{4}));
    EXPECT_EQ(evicted, (std::vector<int>{1, 2, 3}));
}

/* TEST
 * Tests interval-based batching and forced flushing of dropped-event counts.
 * Input: record 2 drops at 1000 ms, 1 before 1099/1100 ms, then 3 before force_flush.
 * Values: flush interval=100 ms; checks immediately before and at the interval boundary.
 * Expected: flush counts are 2, 0, 1, 3, then 0 after the pending count is cleared.
 */
TEST(DropLogThrottle, FlushesAtMostOncePerIntervalAndCanForceFlush) {
    DropLogThrottle throttle(100);
    throttle.record_drop(2);
    EXPECT_EQ(throttle.maybe_flush(1000), 2u);

    throttle.record_drop();
    EXPECT_EQ(throttle.maybe_flush(1099), 0u);
    EXPECT_EQ(throttle.maybe_flush(1100), 1u);

    throttle.record_drop(3);
    EXPECT_EQ(throttle.force_flush(), 3u);
    EXPECT_EQ(throttle.force_flush(), 0u);
}

int main(int argc, char **argv) {
    // Initialize GStreamer for the linked event-recorder component before running this test suite.
    gst_init(&argc, &argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}