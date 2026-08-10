/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <glib.h>
#include <gst/check/gstcheck.h>
#include <stdio.h>

#include "pipeline_test_common.h"
#include "test_utils.h"

/*
 * Test checks whether events can go up- and down-stream through GVA elements. GVA elements
 * must not drop these event, but propagate them further instead.
 *
 * List of not tested events:
 * caps event // can't be easily tested. See Gstreamer Negotiation for details
 * protection event
 * select streams event
 * stream collection event
 * stream group done event
 *
 * navigation // is not overloaded in filesrc and gst_base_src doesn't handle it
 * step // is not overloaded in filesrc and gst_base_src doesn't handle it
 * toc select // is not overloaded in filesrc and gst_base_src doesn't handle it
 */

char *get_command_line() {
    static char command_line[8 * MAX_STR_PATH_SIZE];
    char detection_model_path[MAX_STR_PATH_SIZE];
    char classify_model_path[MAX_STR_PATH_SIZE];
    char video_file_path[MAX_STR_PATH_SIZE];

    ExitStatus status =
        get_model_path(detection_model_path, MAX_STR_PATH_SIZE, "yolov8_license_plate_detector", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_model_path(classify_model_path, MAX_STR_PATH_SIZE, "dima806_fairface_gender_image_detection", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_video_file_path(video_file_path, MAX_STR_PATH_SIZE, "Pexels_Videos_4786.mp4");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    snprintf(command_line, sizeof(command_line),
             "filesrc location=%s ! qtdemux ! avdec_h264 ! videoconvert ! "
             "gvadetect model=%s device=CPU inference-interval=1 batch-size=1 ! gvaclassify model=%s ! "
             "gvametaconvert format=dump-detection ! fakesink",
             video_file_path, detection_model_path, classify_model_path);
    return command_line;
}

static GstPadProbeReturn check_event_forwarded(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    GArray *events = user_data;

    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
    GST_DEBUG("Got event %p %s", event, GST_EVENT_TYPE_NAME(event));
    GST_DEBUG("Len %i", events->len);

    for (guint idx = 0; idx < events->len; idx++) {
        if (event == g_array_index(events, GstEvent *, idx)) {
            GST_DEBUG("Deleted %p", g_array_index(events, GstEvent *, idx));
            g_array_remove_index(events, idx);
            GST_DEBUG("New Len %i", events->len);
            break;
        }
    }

    return GST_PAD_PROBE_OK;
}

void release_events(GstPad *pad, GArray *events) {
    GstEvent *current_event = NULL;
    while (events->len > 0) {
        current_event = g_array_index(events, GstEvent *, 0);
        g_print("Sending event %p %s\n", current_event, GST_EVENT_TYPE_NAME(current_event));
        ck_assert(gst_pad_send_event(pad, current_event));
        g_usleep(5e5);
        // current_event is removed from array after it successfully detected on destination pad
    }
    ck_assert(events->len == 0); // if all events were propagated successfully to fakesink, array must be empty
}

GST_START_TEST(test_downstream_events_are_not_dropped) {
    char *command_line = get_command_line();
    GstElement *pipeline = gst_parse_launch(command_line, NULL);
    g_return_if_fail(pipeline);

    GstElement *fakesink = gst_bin_get_by_name(GST_BIN(pipeline), "fakesink0");
    ck_assert(fakesink != NULL);
    GstPad *sink = gst_element_get_static_pad(fakesink, "sink");
    ck_assert(sink != NULL);

    GstElement *videoconvert = gst_bin_get_by_name(GST_BIN(pipeline), "videoconvert0");
    ck_assert(videoconvert != NULL);
    GstPad *videoconvert_sink = gst_element_get_static_pad(videoconvert, "sink");
    ck_assert(videoconvert_sink != NULL);

    // helper data for events init
    GstSegment *seg = gst_segment_new();
    gst_segment_init(seg, GST_FORMAT_PERCENT);
    GstTagList *taglist = gst_tag_list_new_empty();
    GstToc *toc = gst_toc_new(GST_TOC_SCOPE_CURRENT);

    // init events
    GstEvent *flush_stop_event = gst_event_new_flush_stop(FALSE);
    GstEvent *flush_start_event = gst_event_new_flush_start();
    GstEvent *gap_event = gst_event_new_gap(0, 1e6);
    GstEvent *seg_event = gst_event_new_segment(seg);
    GstEvent *tag_event = gst_event_new_tag(taglist);
    GstEvent *buffer_size_event = gst_event_new_buffer_size(GST_FORMAT_BUFFERS, 1, 1, FALSE);
    GstEvent *toc_event = gst_event_new_toc(toc, FALSE);
    GstEvent *seg_done_event = gst_event_new_segment_done(GST_FORMAT_PERCENT, 0);
    GstEvent *sink_msg_event = gst_event_new_sink_message("test", gst_message_new_eos(NULL));
    GstEvent *stream_start_event = gst_event_new_stream_start("test");
    GstEvent *eos = gst_event_new_eos();

    // fill array with events
    GArray *events = g_array_new(FALSE, TRUE, sizeof(GstEvent *));
    g_array_append_val(events, flush_start_event);
    g_array_append_val(events, flush_stop_event);
    g_array_append_val(events, gap_event);
    g_array_append_val(events, seg_event);
    g_array_append_val(events, tag_event);
    g_array_append_val(events, buffer_size_event);
    g_array_append_val(events, toc_event);
    g_array_append_val(events, seg_done_event);
    g_array_append_val(events, sink_msg_event);
    g_array_append_val(events, stream_start_event);
    g_array_append_val(events, eos);

    launch_pipeline(pipeline);

    gst_pad_add_probe(sink, GST_PAD_PROBE_TYPE_EVENT_FLUSH | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, check_event_forwarded,
                      events, NULL);

    release_events(videoconvert_sink, events);

    completion_pipeline(pipeline);

    g_array_free(events, FALSE);
    gst_toc_unref(toc);
    gst_segment_free(seg);
    gst_object_unref(pipeline);
}

GST_END_TEST;

GST_START_TEST(test_upstream_events_are_not_dropped) {
    char *command_line = get_command_line();
    GstElement *pipeline = gst_parse_launch(command_line, NULL);
    g_return_if_fail(pipeline);

    GstElement *gvametaconvert = gst_bin_get_by_name(GST_BIN(pipeline), "gvametaconvert0");
    ck_assert(gvametaconvert != NULL);
    GstPad *src = gst_element_get_static_pad(gvametaconvert, "src");
    ck_assert(src != NULL);

    GstElement *videoconvert = gst_bin_get_by_name(GST_BIN(pipeline), "videoconvert0");
    ck_assert(videoconvert != NULL);
    GstPad *videoconvert_src = gst_element_get_static_pad(videoconvert, "src");
    ck_assert(videoconvert_src != NULL);

    // init events
    GstEvent *qos_event = gst_event_new_qos(GST_QOS_TYPE_OVERFLOW, 1.0, 1, 1);
    GstEvent *seek_event =
        gst_event_new_seek(1.0, GST_FORMAT_PERCENT, GST_SEEK_FLAG_NONE, GST_SEEK_TYPE_NONE, 0, GST_SEEK_TYPE_NONE, 0);
    GstEvent *latency_event = gst_event_new_latency(0);
    GstEvent *reconf_event = gst_event_new_reconfigure();

    // fill array with events
    GArray *events = g_array_new(FALSE, TRUE, sizeof(GstEvent *));
    g_array_append_val(events, qos_event);
    g_array_append_val(events, seek_event);
    g_array_append_val(events, latency_event);
    g_array_append_val(events, reconf_event);

    launch_pipeline(pipeline);

    gst_pad_add_probe(videoconvert_src, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM, check_event_forwarded, events, NULL);

    release_events(src, events);

    completion_pipeline(pipeline);

    g_array_free(events, FALSE);
    gst_object_unref(pipeline);
}

GST_END_TEST;

static Suite *pipeline_state_suite(void) {
    Suite *s = suite_create("pipeline_events_test");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_downstream_events_are_not_dropped);
    tcase_add_test(tc_chain, test_upstream_events_are_not_dropped);
    return s;
}

GST_CHECK_MAIN(pipeline_state);
