/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gst/check/gstcheck.h>
#include <gst/video/gstvideometa.h>
#include <stdio.h>

#include "pipeline_test_common.h"
#include "test_utils.h"

#define DEFAULT_FRAME_COUNT_LIMIT 500

void check_out_buf_meta(GstBuffer *buffer, gpointer user_data) {
    ck_assert(buffer != NULL);

    GstVideoRegionOfInterestMeta *meta = NULL;
    gpointer state = NULL;
    int num_objects = 0;
    while ((meta = GST_VIDEO_REGION_OF_INTEREST_META_ITERATE(buffer, &state))) {
        num_objects++;
        int num_attributes = 0;
        for (GList *l = meta->params; l; l = g_list_next(l)) {
            num_attributes++;
        }
        ck_assert(num_attributes > 0);
    }
    ck_assert(num_objects > 0);
}

GST_START_TEST(test_inference_id_multichannel) {
    gchar command_line[8 * MAX_STR_PATH_SIZE];
    char detection_model_path[MAX_STR_PATH_SIZE];
    char classify_model_path[MAX_STR_PATH_SIZE];
    char video_file_path[MAX_STR_PATH_SIZE];
    const char *appsink_names[] = {"appsink1", "appsink2"};

    ExitStatus status = get_model_path(detection_model_path, MAX_STR_PATH_SIZE, "yolo26s", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_model_path(classify_model_path, MAX_STR_PATH_SIZE, "dima806_fairface_gender_image_detection",
                            "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_video_file_path(video_file_path, MAX_STR_PATH_SIZE, "Pexels_Videos_4786.mp4");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    snprintf(command_line, sizeof(command_line),
             "filesrc location=%s ! qtdemux ! h264parse ! avdec_h264 ! videoconvert ! "
             "gvadetect model-instance-id=inf0 model=%s device=CPU inference-interval=1 batch-size=1 ! "
             "gvaclassify model=%s device=CPU model-instance-id=cls0 ! queue ! appsink sync=false name=%s "
             "filesrc location=%s ! qtdemux ! h264parse ! avdec_h264 ! videoconvert ! "
             "gvadetect model-instance-id=inf0 model=%s device=CPU inference-interval=1 batch-size=1 ! gvaclassify "
             "model=%s device=CPU model-instance-id=cls0 ! queue ! appsink sync=false name=%s",
             video_file_path, detection_model_path, classify_model_path, appsink_names[0], video_file_path,
             detection_model_path, classify_model_path, appsink_names[1]);

    AppsinkTestData test_data = {
        .check_buf_cb = check_out_buf_meta, .frame_count_limit = DEFAULT_FRAME_COUNT_LIMIT, .user_data = NULL};

    check_run_pipeline_with_appsink_default(command_line, GST_CLOCK_TIME_NONE, appsink_names,
                                            G_N_ELEMENTS(appsink_names), &test_data);
}

GST_END_TEST;

static Suite *params_test_suite(void) {
    Suite *s = suite_create("pipeline_params_test");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_inference_id_multichannel);

    return s;
}

GST_CHECK_MAIN(params_test);
