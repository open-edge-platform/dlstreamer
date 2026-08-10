/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gst/check/gstcheck.h>
#include <stdio.h>

#include "pipeline_test_common.h"
#include "test_utils.h"

#define GVA_CLASSIFY_ELEMENT_NAME "classify"
#define EXPECTED_FRAMES_COUNT 100
#define NIREQ 100

int frames_count = 0;
int eos = 0;

static GstPadProbeReturn pad_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    if (!eos)
        ++frames_count;
    return GST_PAD_PROBE_OK;
}

static void attach_counter_to_src(GstElement *pipeline) {
    GstElement *gvaclassify = gst_bin_get_by_name(GST_BIN(pipeline), GVA_CLASSIFY_ELEMENT_NAME);
    GstPad *pad = gst_element_get_static_pad(gvaclassify, "src");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, pad_probe_callback, NULL, NULL);
    gst_object_unref(pad);
}

static void count_frames_in_pipeline(const char *pipeline_str) {
    // GST_ERROR("%s", pipeline_str);
    GstElement *pipeline;

    pipeline = gst_parse_launch(pipeline_str, NULL);
    ck_assert(pipeline != NULL);

    attach_counter_to_src(pipeline);

    GstMessage *msg = NULL;
    GstBus *bus = gst_element_get_bus(pipeline);
    ck_assert(bus != NULL);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // msg = gst_bus_timed_pop_filtered(bus, GST_SECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    msg = gst_bus_poll(bus, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS), -1);
    ck_assert(msg == NULL || GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS);
    eos = 1;

    if (msg)
        gst_message_unref(msg);

    gst_element_set_state(pipeline, GST_STATE_NULL);

    gst_object_unref(bus);
    gst_object_unref(pipeline);
}

GST_START_TEST(test_frame_drop) {
    g_print("Starting test: %s\n", "test_frame_drop");
    gchar command_line[8 * MAX_STR_PATH_SIZE];

    char detection_model_path[MAX_STR_PATH_SIZE];
    char classify_model_path[MAX_STR_PATH_SIZE];

    ExitStatus status = get_model_path(detection_model_path, MAX_STR_PATH_SIZE, "yolo11s", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_model_path(classify_model_path, MAX_STR_PATH_SIZE, "dima806_fairface_gender_image_detection", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    snprintf(command_line, sizeof(command_line),
             "videotestsrc num-buffers=%d pattern=\"Moving ball\" ! "
             "video/x-raw,width=1920,height=1080,framerate=30/1 !"
             "videoconvert ! "
             "gvadetect model=%s device=CPU inference-interval=1 batch-size=1 nireq=%d ! "
             "gvaclassify model=%s device=CPU nireq=%d name=%s ! "
             "fakesink sync=false",
             EXPECTED_FRAMES_COUNT, detection_model_path, NIREQ, classify_model_path, NIREQ, GVA_CLASSIFY_ELEMENT_NAME);
    count_frames_in_pipeline(command_line);
    ck_assert_int_eq(EXPECTED_FRAMES_COUNT, frames_count);
}

GST_END_TEST;

static Suite *frame_drop_test_suite(void) {
    Suite *s = suite_create("frame_drop");
    TCase *test_case = tcase_create("general");

    suite_add_tcase(s, test_case);
    tcase_add_test(test_case, test_frame_drop);

    return s;
}

GST_CHECK_MAIN(frame_drop_test);
