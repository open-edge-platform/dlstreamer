/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gst/check/gstcheck.h>
#include <stdio.h>

#include "pipeline_test_common.h"
#include "test_utils.h"

#define PIPELINE_EXECUTING_TIMEOUT (GST_SECOND * 1)

static const char *break_prob[] = {"0.5", "1"};

GST_START_TEST(test_breakmydata_detection) {
    g_print("Starting test: %s\n", "test_breakmydata_detection");
    gchar command_line[8 * MAX_STR_PATH_SIZE];
    char detection_model_path[MAX_STR_PATH_SIZE];
    char video_file_path[MAX_STR_PATH_SIZE];

    /*
        OBJECT_DETECTION
    */
    ExitStatus status = get_model_path(detection_model_path, MAX_STR_PATH_SIZE, "yolo11s", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    get_video_file_path(video_file_path, MAX_STR_PATH_SIZE, "Pexels_Videos_4786.mp4");

    for (int j = 0; j < G_N_ELEMENTS(break_prob); j++) {
        snprintf(command_line, sizeof(command_line),
                 "filesrc location=%s ! qtdemux ! avdec_h264 ! video/x-raw ! videoconvert ! "
                 "breakmydata probability=%s ! gvadetect model=%s ! fakesink sync=false",
                 video_file_path, break_prob[j], detection_model_path);

        g_print("Pipeline: %s\n", command_line);
        check_run_pipeline(command_line, PIPELINE_EXECUTING_TIMEOUT);
    }
}
END_TEST;

GST_START_TEST(test_breakmydata_classify) {
    g_print("Starting test: %s\n", "test_breakmydata_classify");
    gchar command_line[8 * MAX_STR_PATH_SIZE];
    char detection_model_path[MAX_STR_PATH_SIZE];
    char classify_model_path_1[MAX_STR_PATH_SIZE];
    char video_file_path[MAX_STR_PATH_SIZE];

    /*
        security_barrier_camera
    */
    ExitStatus status = get_model_path(detection_model_path, MAX_STR_PATH_SIZE, "yolo11s", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status =
        get_model_path(classify_model_path_1, MAX_STR_PATH_SIZE, "dima806_vehicle_10_types_image_detection", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    status = get_video_file_path(video_file_path, MAX_STR_PATH_SIZE, "Pexels_Videos_4786.mp4");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    for (int i = 0; i < G_N_ELEMENTS(break_prob); i++) {
        snprintf(command_line, sizeof(command_line),
                 "filesrc location=%s ! qtdemux ! avdec_h264 ! video/x-raw ! videoconvert ! "
                 "gvadetect model=%s ! queue ! "
                 "breakmydata probability=%s ! gvaclassify model=%s object-class=vehicle ! "
                 "fakesink sync = false",
                 video_file_path, detection_model_path, break_prob[i], classify_model_path_1);

        g_print("Pipeline: %s\n", command_line);
        check_run_pipeline(command_line, PIPELINE_EXECUTING_TIMEOUT);
    }
}
END_TEST;

GST_START_TEST(test_breakmydata_inference) {
    g_print("Starting test: %s\n", "test_breakmydata_inference");
    gchar command_line[8 * MAX_STR_PATH_SIZE];
    char detection_model_path[MAX_STR_PATH_SIZE];
    char video_file_path[MAX_STR_PATH_SIZE];

    ExitStatus status = get_model_path(detection_model_path, MAX_STR_PATH_SIZE, "yolo11s", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_video_file_path(video_file_path, MAX_STR_PATH_SIZE, "Pexels_Videos_4786.mp4");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    for (int j = 0; j < G_N_ELEMENTS(break_prob); j++) {
        snprintf(command_line, sizeof(command_line),
                 "filesrc location=%s ! qtdemux ! avdec_h264 ! video/x-raw ! videoconvert ! "
                 "breakmydata probability=%s ! gvainference model=%s ! "
                 "fakesink sync=false",
                 video_file_path, break_prob[j], detection_model_path);

        g_print("Pipeline: %s\n", command_line);
        check_run_pipeline(command_line, PIPELINE_EXECUTING_TIMEOUT);
    }
}
END_TEST;

GST_START_TEST(test_breakmydata_watermark) {
    g_print("Starting test: %s\n", "test_breakmydata_watermark");
    gchar command_line[8 * MAX_STR_PATH_SIZE];
    char detection_model_path[MAX_STR_PATH_SIZE];
    char video_file_path[MAX_STR_PATH_SIZE];

    /*
        security_barrier_camera
    */
    ExitStatus status = get_model_path(detection_model_path, MAX_STR_PATH_SIZE, "yolo11s", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_video_file_path(video_file_path, MAX_STR_PATH_SIZE, "Pexels_Videos_4786.mp4");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    for (int i = 0; i < G_N_ELEMENTS(break_prob); i++) {
        snprintf(command_line, sizeof(command_line),
                 "filesrc location=%s ! qtdemux ! avdec_h264 ! video/x-raw ! videoconvert ! "
                 "gvadetect model=%s ! queue ! "
                 "breakmydata probability=%s ! gvawatermark ! "
                 "videoconvert ! fakesink sync=false",
                 video_file_path, detection_model_path, break_prob[i]);

        g_print("Pipeline: %s\n", command_line);
        check_run_pipeline(command_line, PIPELINE_EXECUTING_TIMEOUT);
    }
}
END_TEST;

GST_START_TEST(test_breakmydata_metaconvert) {
    g_print("Starting test: %s\n", "test_breakmydata_metaconvert");
    gchar command_line[8 * MAX_STR_PATH_SIZE];
    char detection_model_path[MAX_STR_PATH_SIZE];
    char classify_model_path[MAX_STR_PATH_SIZE];
    char video_file_path[MAX_STR_PATH_SIZE];

    ExitStatus status = get_model_path(detection_model_path, MAX_STR_PATH_SIZE, "yolo11s", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_model_path(classify_model_path, MAX_STR_PATH_SIZE, "dima806_fairface_gender_image_detection", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_video_file_path(video_file_path, MAX_STR_PATH_SIZE, "Pexels_Videos_4786.mp4");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    for (int j = 0; j < G_N_ELEMENTS(break_prob); j++) {
        snprintf(command_line, sizeof(command_line),
                 "filesrc location=%s ! qtdemux ! avdec_h264 ! videoconvert ! "
                 "gvadetect model=%s device=CPU inference-interval=1 batch-size=1 ! "
                 "gvaclassify model=%s device=CPU ! breakmydata probability=%s ! "
                 "gvametaconvert format=json ! fakesink sync=false",
                 video_file_path, detection_model_path, classify_model_path, break_prob[j]);

        g_print("Pipeline: %s\n", command_line);
        check_run_pipeline(command_line, PIPELINE_EXECUTING_TIMEOUT);
    }
}
END_TEST;

GST_START_TEST(test_breakmydata_element_combination) {
    g_print("Starting test: %s\n", "test_breakmydata_element_combination");
    gchar command_line[8 * MAX_STR_PATH_SIZE];
    char detection_model_path[MAX_STR_PATH_SIZE];
    char classify_model_path_1[MAX_STR_PATH_SIZE];
    char video_file_path[MAX_STR_PATH_SIZE];

    ExitStatus status = get_model_path(detection_model_path, MAX_STR_PATH_SIZE, "yolo11s", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status =
        get_model_path(classify_model_path_1, MAX_STR_PATH_SIZE, "dima806_vehicle_10_types_image_detection", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    status = get_video_file_path(video_file_path, MAX_STR_PATH_SIZE, "Pexels_Videos_4786.mp4");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    snprintf(command_line, sizeof(command_line),
             "filesrc location=%s ! qtdemux ! avdec_h264 ! video/x-raw ! videoconvert ! "
             "breakmydata probability=%s ! gvadetect model=%s ! queue ! "
             "breakmydata probability=%s ! gvaclassify model=%s object-class=vehicle ! queue ! "
             "breakmydata probability=%s ! gvawatermark ! "
             "videoconvert ! fakesink sync=false",
             video_file_path, break_prob[0], detection_model_path, break_prob[0], classify_model_path_1, break_prob[0]);

    g_print("Pipeline: %s\n", command_line);
    check_run_pipeline(command_line, PIPELINE_EXECUTING_TIMEOUT);
}
END_TEST;

static Suite *breakmydata_test_suite(void) {
    Suite *s = suite_create("breakmydata_tests");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);

    tcase_add_test(tc_chain, test_breakmydata_detection);
    tcase_add_test(tc_chain, test_breakmydata_classify);
    tcase_add_test(tc_chain, test_breakmydata_inference);
    tcase_add_test(tc_chain, test_breakmydata_watermark);
    tcase_add_test(tc_chain, test_breakmydata_metaconvert);

    tcase_add_test(tc_chain, test_breakmydata_element_combination);

    return s;
}

GST_CHECK_MAIN(breakmydata_test);
