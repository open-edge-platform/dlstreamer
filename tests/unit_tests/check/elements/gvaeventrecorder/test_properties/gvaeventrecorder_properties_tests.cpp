/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* TEST
 * Overview: Tests gvaeventrecorder property defaults, types, ranges, and read/write behavior.
 */

#include "gstgvaeventrecorder.h"

#include <gst/gst.h>
#include <gtest/gtest.h>

namespace {

void expect_readwrite_property(GObject *object, const gchar *name, GType value_type) {
    GParamSpec *property = g_object_class_find_property(G_OBJECT_GET_CLASS(object), name);
    ASSERT_NE(property, nullptr) << name;
    EXPECT_EQ(G_PARAM_SPEC_VALUE_TYPE(property), value_type) << name;
    EXPECT_EQ(property->flags & G_PARAM_READWRITE, G_PARAM_READWRITE) << name;
}

void expect_enum_property(GObject *object, const gchar *name) {
    GParamSpec *property = g_object_class_find_property(G_OBJECT_GET_CLASS(object), name);
    ASSERT_NE(property, nullptr) << name;
    EXPECT_TRUE(G_TYPE_IS_ENUM(G_PARAM_SPEC_VALUE_TYPE(property))) << name;
    EXPECT_EQ(property->flags & G_PARAM_READWRITE, G_PARAM_READWRITE) << name;
}

void expect_uint_range(GObject *object, const gchar *name, guint minimum, guint maximum) {
    GParamSpec *property = g_object_class_find_property(G_OBJECT_GET_CLASS(object), name);
    ASSERT_NE(property, nullptr) << name;
    ASSERT_TRUE(G_IS_PARAM_SPEC_UINT(property)) << name;
    EXPECT_EQ(G_PARAM_SPEC_UINT(property)->minimum, minimum) << name;
    EXPECT_EQ(G_PARAM_SPEC_UINT(property)->maximum, maximum) << name;
}

void expect_uint64_range(GObject *object, const gchar *name, guint64 minimum, guint64 maximum) {
    GParamSpec *property = g_object_class_find_property(G_OBJECT_GET_CLASS(object), name);
    ASSERT_NE(property, nullptr) << name;
    ASSERT_TRUE(G_IS_PARAM_SPEC_UINT64(property)) << name;
    EXPECT_EQ(G_PARAM_SPEC_UINT64(property)->minimum, minimum) << name;
    EXPECT_EQ(G_PARAM_SPEC_UINT64(property)->maximum, maximum) << name;
}

void expect_double_range(GObject *object, const gchar *name, gdouble minimum, gdouble maximum) {
    GParamSpec *property = g_object_class_find_property(G_OBJECT_GET_CLASS(object), name);
    ASSERT_NE(property, nullptr) << name;
    ASSERT_TRUE(G_IS_PARAM_SPEC_DOUBLE(property)) << name;
    EXPECT_DOUBLE_EQ(G_PARAM_SPEC_DOUBLE(property)->minimum, minimum) << name;
    EXPECT_DOUBLE_EQ(G_PARAM_SPEC_DOUBLE(property)->maximum, maximum) << name;
}

class GvaEventRecorderPropertiesTest : public ::testing::Test {
  protected:
    void SetUp() override {
        element = G_OBJECT(g_object_new(GST_TYPE_GVAEVENTRECORDER, nullptr));
        ASSERT_NE(element, nullptr);
    }

    void TearDown() override {
        if (element)
            g_object_unref(element);
    }

    GObject *element = nullptr;
};

/* TEST
 * Tests default values, GObject types/access flags, and declared numeric bounds for all recorder properties.
 * Input: a newly constructed recorder with no property overrides.
 * Values: defaults include person,vehicle; threshold 0.5; PRE/POST 30; gap 3; cooldown 150; max events 4.
 *         Output directory is ".", encoder is software, and drop/flush intervals are 1500/3000 ms.
 * Expected: getters return defaults and properties are read/write; confidence is [0,1], max events [1,G_MAXUINT],
 *           cooldown [0,G_MAXUINT64], and other integer properties [0,G_MAXUINT].
 */
TEST_F(GvaEventRecorderPropertiesTest, ExposesExpectedDefaultsAndPropertySpecs) {
    gchar *target_classes = nullptr;
    gchar *output_dir = nullptr;
    gdouble confidence_threshold = 0.0;
    guint pre_event_frames = 0;
    guint post_event_frames = 0;
    guint event_gap_frames = 0;
    guint64 cooldown_value = 0;
    guint max_concurrent_events = 0;
    gint encoder_mode = -1;
    guint drop_log_interval_ms = 0;
    guint flush_interval_ms = 0;
    g_object_get(element, "target-classes", &target_classes, "confidence-threshold", &confidence_threshold,
                 "pre-event-frames", &pre_event_frames, "post-event-frames", &post_event_frames, "event-gap-frames",
                 &event_gap_frames, "cooldown-value", &cooldown_value, "max-concurrent-events",
                 &max_concurrent_events, "event-clip-output-dir", &output_dir, "encoder-mode", &encoder_mode,
                 "drop-log-interval-ms", &drop_log_interval_ms, "flush-interval-ms", &flush_interval_ms, nullptr);

    EXPECT_STREQ(target_classes, "person,vehicle");
    EXPECT_DOUBLE_EQ(confidence_threshold, 0.5);
    EXPECT_EQ(pre_event_frames, 30u);
    EXPECT_EQ(post_event_frames, 30u);
    EXPECT_EQ(event_gap_frames, 3u);
    EXPECT_EQ(cooldown_value, 150u);
    EXPECT_EQ(max_concurrent_events, 4u);
    EXPECT_STREQ(output_dir, ".");
    EXPECT_EQ(encoder_mode, GST_GVAEVENTRECORDER_ENCODER_SOFTWARE);
    EXPECT_EQ(drop_log_interval_ms, 1500u);
    EXPECT_EQ(flush_interval_ms, 3000u);
    g_free(target_classes);
    g_free(output_dir);

    expect_readwrite_property(element, "target-classes", G_TYPE_STRING);
    expect_readwrite_property(element, "confidence-threshold", G_TYPE_DOUBLE);
    expect_readwrite_property(element, "pre-event-frames", G_TYPE_UINT);
    expect_readwrite_property(element, "post-event-frames", G_TYPE_UINT);
    expect_readwrite_property(element, "event-gap-frames", G_TYPE_UINT);
    expect_readwrite_property(element, "cooldown-value", G_TYPE_UINT64);
    expect_readwrite_property(element, "max-concurrent-events", G_TYPE_UINT);
    expect_readwrite_property(element, "event-clip-output-dir", G_TYPE_STRING);
    expect_enum_property(element, "encoder-mode");
    expect_readwrite_property(element, "drop-log-interval-ms", G_TYPE_UINT);
    expect_readwrite_property(element, "flush-interval-ms", G_TYPE_UINT);

    expect_double_range(element, "confidence-threshold", 0.0, 1.0);
    expect_uint_range(element, "pre-event-frames", 0, G_MAXUINT);
    expect_uint_range(element, "post-event-frames", 0, G_MAXUINT);
    expect_uint_range(element, "event-gap-frames", 0, G_MAXUINT);
    expect_uint64_range(element, "cooldown-value", 0, G_MAXUINT64);
    expect_uint_range(element, "max-concurrent-events", 1, G_MAXUINT);
    expect_uint_range(element, "drop-log-interval-ms", 0, G_MAXUINT);
    expect_uint_range(element, "flush-interval-ms", 0, G_MAXUINT);
}

/* TEST
 * Tests setting and reading back every gvaeventrecorder property.
 * Input: person,bicycle; threshold 0.75; PRE/POST 11/12; gap 2; cooldown 900; max events 2.
 *         Also set output path, AUTO encoder, and drop/flush intervals 250/500 ms.
 * Values: all assigned values are within their declared property ranges.
 * Expected: getters return each assigned string, enum, confidence, and integer value unchanged.
 */
TEST_F(GvaEventRecorderPropertiesTest, SetPropertiesCanBeReadBack) {
    g_object_set(element, "target-classes", "person,bicycle", "confidence-threshold", 0.75, "pre-event-frames",
                 11u, "post-event-frames", 12u, "event-gap-frames", 2u, "cooldown-value", static_cast<guint64>(900),
                 "max-concurrent-events", 2u, "event-clip-output-dir", "/tmp/gva-event-clips", "encoder-mode",
                 GST_GVAEVENTRECORDER_ENCODER_AUTO, "drop-log-interval-ms", 250u, "flush-interval-ms", 500u, nullptr);

    gchar *target_classes = nullptr;
    gchar *output_dir = nullptr;
    gdouble confidence_threshold = 0.0;
    guint pre_event_frames = 0;
    guint post_event_frames = 0;
    guint event_gap_frames = 0;
    guint64 cooldown_value = 0;
    guint max_concurrent_events = 0;
    gint encoder_mode = -1;
    guint drop_log_interval_ms = 0;
    guint flush_interval_ms = 0;
    g_object_get(element, "target-classes", &target_classes, "confidence-threshold", &confidence_threshold,
                 "pre-event-frames", &pre_event_frames, "post-event-frames", &post_event_frames, "event-gap-frames",
                 &event_gap_frames, "cooldown-value", &cooldown_value, "max-concurrent-events",
                 &max_concurrent_events, "event-clip-output-dir", &output_dir, "encoder-mode", &encoder_mode,
                 "drop-log-interval-ms", &drop_log_interval_ms, "flush-interval-ms", &flush_interval_ms, nullptr);

    EXPECT_STREQ(target_classes, "person,bicycle");
    EXPECT_DOUBLE_EQ(confidence_threshold, 0.75);
    EXPECT_EQ(pre_event_frames, 11u);
    EXPECT_EQ(post_event_frames, 12u);
    EXPECT_EQ(event_gap_frames, 2u);
    EXPECT_EQ(cooldown_value, 900u);
    EXPECT_EQ(max_concurrent_events, 2u);
    EXPECT_STREQ(output_dir, "/tmp/gva-event-clips");
    EXPECT_EQ(encoder_mode, GST_GVAEVENTRECORDER_ENCODER_AUTO);
    EXPECT_EQ(drop_log_interval_ms, 250u);
    EXPECT_EQ(flush_interval_ms, 500u);
    g_free(target_classes);
    g_free(output_dir);
}

} // namespace

int main(int argc, char **argv) {
    // Initialize GStreamer before constructing elements, then initialize GoogleTest before running the tests.
    gst_init(&argc, &argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}