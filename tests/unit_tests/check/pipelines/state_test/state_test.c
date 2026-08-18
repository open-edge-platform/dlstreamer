/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gst/check/gstcheck.h>
#include <stdio.h>

#include "pipeline_test_common.h"
#include "test_utils.h"

static void check_state_change(GstElement *pipeline, GstState state, GstStateChangeReturn immediate,
                               GstStateChangeReturn final) {
    GstStateChangeReturn ret;

    ret = gst_element_set_state(pipeline, state);
    if (ret != immediate) {
        g_critical("Unexpected set_state return ->%s: %d != %d", gst_element_state_get_name(state), ret, immediate);
    }
    ret = gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
    if (ret != final) {
        g_critical("Unexpected get_state return ->%s: %d != %d", gst_element_state_get_name(state), ret, final);
    }
}

GST_START_TEST(test_change_state) {
    GstElement *pipeline;

    gchar command_line[8 * MAX_STR_PATH_SIZE];
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

    pipeline = gst_parse_launch(command_line, NULL);
    g_return_if_fail(pipeline);

    check_state_change(pipeline, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS, GST_STATE_CHANGE_SUCCESS);
    check_state_change(pipeline, GST_STATE_READY, GST_STATE_CHANGE_SUCCESS, GST_STATE_CHANGE_SUCCESS);
    check_state_change(pipeline, GST_STATE_PAUSED, GST_STATE_CHANGE_ASYNC, GST_STATE_CHANGE_SUCCESS);
    check_state_change(pipeline, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS, GST_STATE_CHANGE_SUCCESS);
    check_state_change(pipeline, GST_STATE_READY, GST_STATE_CHANGE_SUCCESS, GST_STATE_CHANGE_SUCCESS);
    check_state_change(pipeline, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS, GST_STATE_CHANGE_SUCCESS);

    gst_object_unref(pipeline);
}

GST_END_TEST;

GST_START_TEST(test_start_stop_start) {
    GstElement *pipeline;

    gchar command_line[8 * MAX_STR_PATH_SIZE];
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

    pipeline = gst_parse_launch(command_line, NULL);

    g_return_if_fail(pipeline);

    // check states transition
    gint i = 100;
    while (i--) {
        // transition playing->paused->ready
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
        gst_element_set_state(pipeline, GST_STATE_READY);

        // transition playing->paused->playing
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
        gst_element_set_state(pipeline, GST_STATE_PLAYING);

        // transition paused->null->paused
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_element_set_state(pipeline, GST_STATE_PAUSED);

        // transition playing->paused->null->playing
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
        gst_element_set_state(pipeline, GST_STATE_NULL);

        // transition playing->null->playing
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        gst_element_set_state(pipeline, GST_STATE_NULL);
    }

    gst_object_unref(pipeline);
}

GST_END_TEST;

static Suite *pipeline_state_suite(void) {
    Suite *s = suite_create("pipeline_state_test");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_change_state);
    tcase_add_test(tc_chain, test_start_stop_start);
    return s;
}

GST_CHECK_MAIN(pipeline_state);
