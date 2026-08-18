/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gst/check/gstcheck.h>
#include <stdio.h>

#include "common.h"
#include "test_utils.h"

const guint input_image_width = 1280;
const guint input_image_height = 736;

// centerface native input is 1280x768 and requires width/height divisible by 32.
const guint input_layer_width = 1280;
const guint input_layer_height = 768;

GST_START_TEST(test_reshape_to_orignal_frame_size) {
    gchar pipeline_str[8 * MAX_STR_PATH_SIZE];
    char model_path[MAX_STR_PATH_SIZE];
    const guint expected_batch_size = 1;

    ExitStatus status = get_model_path(model_path, MAX_STR_PATH_SIZE, "centerface", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    snprintf(pipeline_str, sizeof(pipeline_str),
             "videotestsrc num-buffers=1 pattern=snow ! video/x-raw,format=BGRx,width=%d,height=%d ! "
             "gvadetect pre-process-backend=opencv name=gvadetect model=%s device=CPU reshape=true ! fakesink "
             "sync=false",
             input_image_width, input_image_height, model_path);

    check_model_input_info(pipeline_str, input_image_width, input_image_height, expected_batch_size);
}

GST_END_TEST;

GST_START_TEST(test_reshape_to_custom_width) {
    gchar pipeline_str[8 * MAX_STR_PATH_SIZE];
    char model_path[MAX_STR_PATH_SIZE];
    const guint expected_batch_size = 1;

    ExitStatus status = get_model_path(model_path, MAX_STR_PATH_SIZE, "centerface", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    snprintf(pipeline_str, sizeof(pipeline_str),
             "videotestsrc num-buffers=1 pattern=snow ! video/x-raw,format=BGRx,width=%d,height=%d ! "
             "gvadetect pre-process-backend=opencv name=gvadetect model=%s device=CPU reshape-width=%d ! fakesink "
             "sync=false",
             input_image_width, input_image_height, model_path, input_image_width);

    check_model_input_info(pipeline_str, input_image_width, input_layer_height, expected_batch_size);
}

GST_END_TEST;

GST_START_TEST(test_reshape_to_custom_height) {
    gchar pipeline_str[8 * MAX_STR_PATH_SIZE];
    char model_path[MAX_STR_PATH_SIZE];
    const guint expected_batch_size = 1;

    ExitStatus status = get_model_path(model_path, MAX_STR_PATH_SIZE, "centerface", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    snprintf(pipeline_str, sizeof(pipeline_str),
             "videotestsrc num-buffers=1 pattern=snow ! video/x-raw,format=BGRx,width=%d,height=%d ! "
             "gvadetect pre-process-backend=opencv name=gvadetect model=%s device=CPU reshape-height=%d ! fakesink "
             "sync=false",
             input_image_width, input_image_height, model_path, input_image_height);

    check_model_input_info(pipeline_str, input_layer_width, input_image_height, expected_batch_size);
}

GST_END_TEST;

GST_START_TEST(test_reshape_to_custom_width_and_height) {
    gchar pipeline_str[8 * MAX_STR_PATH_SIZE];
    char model_path[MAX_STR_PATH_SIZE];
    const guint expected_batch_size = 1;

    ExitStatus status = get_model_path(model_path, MAX_STR_PATH_SIZE, "centerface", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    snprintf(pipeline_str, sizeof(pipeline_str),
             "videotestsrc num-buffers=1 pattern=snow ! video/x-raw,format=BGRx,width=%d,height=%d ! "
             "gvadetect pre-process-backend=opencv name=gvadetect model=%s device=CPU reshape-width=%d "
             "reshape-height=%d ! fakesink sync=false",
             input_image_width, input_image_height, model_path, input_image_width, input_image_height);

    check_model_input_info(pipeline_str, input_image_width, input_image_height, expected_batch_size);
}

GST_END_TEST;

GST_START_TEST(test_reshape_to_custom_batch_size) {
    gchar pipeline_str[8 * MAX_STR_PATH_SIZE];
    char model_path[MAX_STR_PATH_SIZE];
    const guint expected_batch_size = 10;

    ExitStatus status = get_model_path(model_path, MAX_STR_PATH_SIZE, "centerface", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    snprintf(pipeline_str, sizeof(pipeline_str),
             "videotestsrc num-buffers=%d pattern=snow ! video/x-raw,format=BGRx,width=%d,height=%d ! "
             "gvadetect pre-process-backend=opencv name=gvadetect model=%s device=CPU batch-size=%d ! fakesink "
             "sync=false",
             expected_batch_size, input_image_width, input_image_height, model_path, expected_batch_size);

    check_model_input_info(pipeline_str, input_layer_width, input_layer_height, expected_batch_size);
}

GST_END_TEST;

GST_START_TEST(test_reshape_failed_to_custom_batch_size_with_ie_pre_proc) {
    gchar pipeline_str[8 * MAX_STR_PATH_SIZE];
    char model_path[MAX_STR_PATH_SIZE];
    const guint expected_batch_size = 10;

    ExitStatus status = get_model_path(model_path, MAX_STR_PATH_SIZE, "centerface", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    snprintf(pipeline_str, sizeof(pipeline_str),
             "videotestsrc num-buffers=%d pattern=snow ! video/x-raw,format=BGRx,width=%d,height=%d ! "
             "gvadetect pre-process-backend=ie name=gvadetect model=%s device=CPU batch-size=%d ! fakesink "
             "sync=false",
             expected_batch_size, input_image_width, input_image_height, model_path, expected_batch_size);

    GstElement *pipeline = gst_parse_launch(pipeline_str, NULL);
    fail_unless(pipeline);
    GstMessage *msg = NULL;
    GstBus *bus = gst_element_get_bus(pipeline);
    ck_assert(bus != NULL);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    msg = gst_bus_timed_pop_filtered(bus, GST_SECOND, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    ck_assert(msg == NULL || GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS);
    if (msg)
        gst_message_unref(msg);

    gst_element_set_state(pipeline, GST_STATE_NULL);

    gst_object_unref(bus);
    gst_object_unref(pipeline);
}

GST_END_TEST;

static Suite *reshape_testing_suite(void) {
    Suite *s = suite_create("reshape_testing_suite");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_reshape_to_orignal_frame_size);
    tcase_add_test(tc_chain, test_reshape_to_custom_width);
    tcase_add_test(tc_chain, test_reshape_to_custom_height);
    tcase_add_test(tc_chain, test_reshape_to_custom_width_and_height);
    tcase_add_test(tc_chain, test_reshape_to_custom_batch_size);
    tcase_add_test(tc_chain, test_reshape_failed_to_custom_batch_size_with_ie_pre_proc);

    return s;
}

GST_CHECK_MAIN(reshape_testing);
