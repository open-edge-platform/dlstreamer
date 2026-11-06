/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* TEST
 * Overview: Tests event-frame selection and ROI scaling in a synthetic appsrc-to-appsink pipeline,
 *           including metadata and EOS.
 */

#include "gstgvaeventrecorder.h"
#include "gstgvaframeselect.h"
#include "gstgvaroiscale.h"
#include "gva_utils.h"

#include <gst/analytics/analytics.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>
#include <glib/gstdio.h>
#include <gtest/gtest.h>

#include <cstring>

namespace {

constexpr guint INPUT_WIDTH = 64;
constexpr guint INPUT_HEIGHT = 64;
constexpr guint OUTPUT_DIMENSION = 16;
constexpr GstClockTime FRAME_DURATION = GST_SECOND / 30;

bool factory_available(const char *name) {
    GstElementFactory *factory = gst_element_factory_find(name);
    if (!factory)
        return false;
    gst_object_unref(factory);
    return true;
}

GstBuffer *make_frame(GstClockTime pts, bool with_detection) {
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, INPUT_WIDTH * INPUT_HEIGHT * 3, nullptr);
    GstMapInfo map = GST_MAP_INFO_INIT;
    if (gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        for (guint y = 0; y < INPUT_HEIGHT; ++y) {
            for (guint x = 0; x < INPUT_WIDTH; ++x) {
                const gsize offset = (static_cast<gsize>(y) * INPUT_WIDTH + x) * 3;
                map.data[offset] = static_cast<guint8>(x * 3);
                map.data[offset + 1] = static_cast<guint8>(y * 3);
                map.data[offset + 2] = 90;
            }
        }
        gst_buffer_unmap(buffer, &map);
    }
    GST_BUFFER_PTS(buffer) = pts;
    GST_BUFFER_DTS(buffer) = pts;
    GST_BUFFER_DURATION(buffer) = FRAME_DURATION;

    if (with_detection) {
        GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(buffer);
        GstAnalyticsODMtd detection;
        gst_analytics_relation_meta_add_od_mtd(relation_meta, g_quark_from_static_string("person"), 16, 16, 32, 32,
                                               0.95f, &detection);
    }
    return buffer;
}

void remove_directory_contents(const gchar *path) {
    if (!path)
        return;
    GDir *directory = g_dir_open(path, 0, nullptr);
    if (directory) {
        while (const gchar *name = g_dir_read_name(directory)) {
            gchar *file_path = g_build_filename(path, name, nullptr);
            g_unlink(file_path);
            g_free(file_path);
        }
        g_dir_close(directory);
    }
    g_rmdir(path);
}

class GvaPipelineTest : public ::testing::Test {
  protected:
    void TearDown() override {
        if (pipeline) {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }
        remove_directory_contents(output_dir);
        g_free(output_dir);
    }

    GstElement *pipeline = nullptr;
    GstElement *source = nullptr;
    GstElement *sink = nullptr;
    gchar *output_dir = nullptr;
};

/* TEST
 * Tests the connected event-recorder -> frame-select -> ROI-scale path and its negotiated output.
 * Input: two 64x64 BGR frames at 30 fps; frame 0 has a person ROI (16,16,32,32) at 0.95, frame 1 has no detection.
 * Values: target=person, threshold=0.5, frame-select block/budget=2/2, ROI output=16x16.
 * Expected: two ordered 16x16 outputs with aggregate ROI metadata and expected PTS/offsets; first is DISCONT,
 *           then EOS with no extra sample.
 */
TEST_F(GvaPipelineTest, EventFramesAreSelectedScaledAndForwardedWithRoiMetadata) {
    if (!(factory_available("x264enc") || factory_available("openh264enc")) || !factory_available("h264parse") ||
        !factory_available("mp4mux"))
        GTEST_SKIP() << "H.264 encoder and MP4 muxer plugins are required by gvaeventrecorder";

    GError *error = nullptr;
    output_dir = g_dir_make_tmp("gva-event-pipeline-XXXXXX", &error);
    ASSERT_NE(output_dir, nullptr) << (error ? error->message : "Could not create temporary directory");
    g_clear_error(&error);

    pipeline = gst_pipeline_new("gva-event-pipeline");
    source = gst_element_factory_make("appsrc", "source");
    GstElement *recorder = GST_ELEMENT(g_object_new(GST_TYPE_GVAEVENTRECORDER, nullptr));
    GstElement *frame_select = GST_ELEMENT(g_object_new(GST_TYPE_GVA_FRAME_SELECT, nullptr));
    GstElement *convert = gst_element_factory_make("videoconvert", "convert");
    GstElement *caps_filter = gst_element_factory_make("capsfilter", "rgb-caps");
    GstElement *roi_scale = GST_ELEMENT(g_object_new(GST_TYPE_GVA_ROI_SCALE, nullptr));
    sink = gst_element_factory_make("appsink", "sink");

    ASSERT_NE(pipeline, nullptr);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(recorder, nullptr);
    ASSERT_NE(frame_select, nullptr);
    ASSERT_NE(convert, nullptr);
    ASSERT_NE(caps_filter, nullptr);
    ASSERT_NE(roi_scale, nullptr);
    ASSERT_NE(sink, nullptr);

    GstCaps *input_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGR", "width", G_TYPE_INT,
                                              INPUT_WIDTH, "height", G_TYPE_INT, INPUT_HEIGHT, "framerate",
                                              GST_TYPE_FRACTION, 30, 1, nullptr);
    g_object_set(source, "caps", input_caps, "format", GST_FORMAT_TIME, nullptr);
    gst_caps_unref(input_caps);
    gst_util_set_object_arg(G_OBJECT(source), "stream-type", "stream");

    g_object_set(recorder, "target-classes", "person", "confidence-threshold", 0.5, "pre-event-frames", 0u,
                 "post-event-frames", 0u, "event-gap-frames", 0u, "cooldown-value", static_cast<guint64>(0),
                 "event-clip-output-dir", output_dir, "flush-interval-ms", 0u, nullptr);
    g_object_set(frame_select, "sub-stream-frames", 2u, "frame-budget", 2u, "target-classes", "person",
                 "confidence-threshold", 0.5, nullptr);
    g_object_set(roi_scale, "out-width", OUTPUT_DIMENSION, "out-height", OUTPUT_DIMENSION, "target-classes", "person",
                 nullptr);
    g_object_set(sink, "sync", FALSE, "async", FALSE, nullptr);

    GstCaps *rgb_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "RGB", nullptr);
    g_object_set(caps_filter, "caps", rgb_caps, nullptr);
    gst_caps_unref(rgb_caps);

    gst_bin_add_many(GST_BIN(pipeline), source, recorder, frame_select, convert, caps_filter, roi_scale, sink, nullptr);
    ASSERT_TRUE(gst_element_link_many(source, recorder, frame_select, convert, caps_filter, roi_scale, sink, nullptr));
    ASSERT_NE(gst_element_set_state(pipeline, GST_STATE_PLAYING), GST_STATE_CHANGE_FAILURE);

    ASSERT_EQ(gst_app_src_push_buffer(GST_APP_SRC(source), make_frame(0, true)), GST_FLOW_OK);
    ASSERT_EQ(gst_app_src_push_buffer(GST_APP_SRC(source), make_frame(FRAME_DURATION, false)), GST_FLOW_OK);
    ASSERT_EQ(gst_app_src_end_of_stream(GST_APP_SRC(source)), GST_FLOW_OK);

    for (guint index = 0; index < 2; ++index) {
        GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 2 * GST_SECOND);
        ASSERT_NE(sample, nullptr) << "Missing selected ROI output frame " << index;
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        ASSERT_NE(buffer, nullptr);
        EXPECT_EQ(GST_BUFFER_PTS(buffer), index * FRAME_DURATION);
        EXPECT_EQ(GST_BUFFER_OFFSET(buffer), index);
        if (index == 0)
            EXPECT_TRUE(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT));

        GstVideoInfo info;
        ASSERT_TRUE(gst_video_info_from_caps(&info, gst_sample_get_caps(sample)));
        EXPECT_EQ(GST_VIDEO_INFO_WIDTH(&info), OUTPUT_DIMENSION);
        EXPECT_EQ(GST_VIDEO_INFO_HEIGHT(&info), OUTPUT_DIMENSION);

        GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
        ASSERT_NE(relation_meta, nullptr);
        gpointer state = nullptr;
        GstAnalyticsODMtd detection;
        ASSERT_TRUE(gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_od_mtd_get_mtd_type(),
                                                        &detection));
        EXPECT_EQ(gst_analytics_od_mtd_get_obj_type(&detection),
                  g_quark_from_static_string(GVA_AGGREGATED_ROI_TYPE));
        gint x = 0, y = 0, width = 0, height = 0;
        gfloat confidence = 0.0f;
        ASSERT_TRUE(gst_analytics_od_mtd_get_location(&detection, &x, &y, &width, &height, &confidence));
        EXPECT_EQ(x, 0);
        EXPECT_EQ(y, 0);
        EXPECT_EQ(width, OUTPUT_DIMENSION);
        EXPECT_EQ(height, OUTPUT_DIMENSION);
        gst_sample_unref(sample);
    }

    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *message = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND,
                                                     static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    ASSERT_NE(message, nullptr) << "Pipeline did not reach EOS";
    GError *pipeline_error = nullptr;
    gchar *debug_info = nullptr;
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
        gst_message_parse_error(message, &pipeline_error, &debug_info);
    ASSERT_EQ(GST_MESSAGE_TYPE(message), GST_MESSAGE_EOS)
        << (pipeline_error ? pipeline_error->message : "") << " " << (debug_info ? debug_info : "");
    g_clear_error(&pipeline_error);
    g_free(debug_info);
    gst_message_unref(message);
    gst_object_unref(bus);

    EXPECT_EQ(gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 0), nullptr);
    EXPECT_TRUE(gst_app_sink_is_eos(GST_APP_SINK(sink)));
}

} // namespace