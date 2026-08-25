/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <glib.h>
#include <gst/check/gstcheck.h>
#include <stdio.h>

#include "test_utils.h"

/*
 * Test checks whether queries can go upstream through GVA elements. GVA elements
 * must not drop these queries, but propagate them further instead.
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
             "filesrc location=%s ! qtdemux ! multiqueue ! h264parse ! capsfilter ! avdec_h264 ! videoconvert ! "
             "gvadetect model=%s device=CPU inference-interval=1 batch-size=1 ! gvaclassify model=%s ! "
             "gvametaconvert format=dump-detection ! fakesink",
             video_file_path, detection_model_path, classify_model_path);
    return command_line;
}

static GstPadProbeReturn check_query_forwarded(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    GArray *queries = user_data;

    GstQuery *query = GST_PAD_PROBE_INFO_QUERY(info);
    GST_DEBUG("Got query %s %p", GST_QUERY_TYPE_NAME(query), query);
    GST_DEBUG("Len %i", queries->len);

    for (guint idx = 0; idx < queries->len; idx++) {
        GstQuery *q = g_array_index(queries, GstQuery *, idx);
        // For latency query we get another pointer here
        if (query == q || (GST_QUERY_TYPE(query) == GST_QUERY_LATENCY && GST_QUERY_TYPE(q) == GST_QUERY_LATENCY)) {
            GST_DEBUG("Deleted %p", q);
            gst_query_unref(q);
            g_array_remove_index(queries, idx);
            GST_DEBUG("New Len %i", queries->len);
            break;
        }
    }

    return GST_PAD_PROBE_OK;
}

GST_START_TEST(test_queries_are_not_dropped) {
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

    // init queries
    GstQuery *duration_query = gst_query_new_duration(GST_FORMAT_TIME);
    GstQuery *convert_query = gst_query_new_convert(GST_FORMAT_TIME, 0, GST_FORMAT_PERCENT);
    GstQuery *latency_query = gst_query_new_latency();
    GstQuery *seeking_query = gst_query_new_seeking(GST_FORMAT_TIME);
    GstQuery *formats_query = gst_query_new_formats();
    GstQuery *segment_query = gst_query_new_segment(GST_FORMAT_TIME);

    // fill array with queries
    GArray *queries = g_array_new(FALSE, TRUE, sizeof(GstQuery *));
    g_array_append_val(queries, duration_query);
    g_array_append_val(queries, convert_query);
    g_array_append_val(queries, latency_query);
    g_array_append_val(queries, seeking_query);
    g_array_append_val(queries, formats_query);
    g_array_append_val(queries, segment_query);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE); // wait for pipeline transition to new state

    // we're gonna catch queries on their way back to pad that makes query.
    // Suppose we have pipeline filesrc ! ... ! fakesink. We do query on fakesink's sink pad, it travels
    // up to filesrc and then goes back to fakesink. When it goes back, we catch it somewhere in the middle of pipeline.
    // We can't catch queries when they travel upstream because when we catch it, we must unref it, and that would be
    // too early
    gst_pad_add_probe(videoconvert_sink, GST_PAD_PROBE_TYPE_QUERY_UPSTREAM | GST_PAD_PROBE_TYPE_PULL,
                      check_query_forwarded, queries, NULL);

    GstQuery *current_query = NULL;
    while (queries->len > 0) {
        current_query = g_array_index(queries, GstQuery *, 0);
        GST_DEBUG("Sending query %s %p", GST_QUERY_TYPE_NAME(current_query), current_query);
        ck_assert(gst_pad_peer_query(sink, current_query));
        // current_query is removed from array after it successfully detected on destination pad
    }

    ck_assert(queries->len == 0); // if all queries were propagated successfully to fakesink, array must be empty

    g_array_free(queries, FALSE);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

GST_END_TEST;

static Suite *pipeline_state_suite(void) {
    Suite *s = suite_create("pipeline_queries_test");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_queries_are_not_dropped);
    return s;
}

GST_CHECK_MAIN(pipeline_state);
