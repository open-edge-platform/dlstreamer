/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gst/check/gstcheck.h>
#include <gst/video/gstvideometa.h>
#include <stdio.h>

#include "pipeline_test_common.h"
#include "test_utils.h"

#define DEFAULT_FRAME_COUNT_LIMIT 500

void check_output_meta_labels(GstBuffer *buffer, gpointer user_data) {
    ck_assert(buffer != NULL);

    GstVideoRegionOfInterestMeta *meta = NULL;
    gpointer state = NULL;
    while ((meta = GST_VIDEO_REGION_OF_INTEREST_META_ITERATE(buffer, &state))) {
        for (GList *l = meta->params; l; l = g_list_next(l)) {
            GstStructure *tensor = GST_STRUCTURE(l->data);
            // Tensor structure shouldn`t contain labels field since this is extra data
            // that can consist of 1000 labels in case of ImageNet
            ck_assert_msg(!gst_structure_has_field(tensor, "labels"), "labels field presents in classification tensor");
        }
    }
}

void test_compact_meta_check_tensor() {
    gchar command_line[8 * MAX_STR_PATH_SIZE];
    char detection_model_path[MAX_STR_PATH_SIZE];
    char classify_gender_model_path[MAX_STR_PATH_SIZE];
    char classify_age_model_path[MAX_STR_PATH_SIZE];
    char video_file_path[MAX_STR_PATH_SIZE];
    const char *appsink_name = "appsink";

    ExitStatus status =
        get_model_path(detection_model_path, MAX_STR_PATH_SIZE, "yolov8_license_plate_detector", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_model_path(classify_gender_model_path, MAX_STR_PATH_SIZE, "colorcls2", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_model_path(classify_age_model_path, MAX_STR_PATH_SIZE, "colorcls2", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_video_file_path(video_file_path, MAX_STR_PATH_SIZE, "Pexels_Videos_4786.mp4");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    snprintf(command_line, sizeof(command_line),
             "filesrc location=%s ! qtdemux ! multiqueue ! h264parse ! capsfilter ! avdec_h264 ! videoconvert ! "
             "gvadetect model=%s ! gvaclassify model=%s ! gvaclassify model=%s ! appsink name=%s sync=false",
             video_file_path, detection_model_path, classify_gender_model_path, classify_age_model_path, appsink_name);

    AppsinkTestData test_data = {
        .check_buf_cb = check_output_meta_labels, .frame_count_limit = DEFAULT_FRAME_COUNT_LIMIT, .user_data = NULL};

    check_run_pipeline_with_appsink_default(command_line, GST_CLOCK_TIME_NONE, &appsink_name, 1, &test_data);
}

GST_START_TEST(test_compact_meta) {
    test_compact_meta_check_tensor();
}

GST_END_TEST;

// Check ROI ID: Every ID should appear on a frame once
gboolean check_produced_roi_id(GHashTable *hash_table) {
    GList *values = g_hash_table_get_values(hash_table);
    for (GList *head = values; head != NULL; head = head->next) {
        if (*(gint *)head->data != 1) {
            return FALSE;
        }
    }

    return TRUE;
}

void check_output_meta_region_id(GstBuffer *buffer, gpointer user_data) {
    ck_assert(buffer != NULL);
    GHashTable *hash_table = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, NULL);
    GstVideoRegionOfInterestMeta *meta = NULL;
    gpointer state = NULL;
    gint *one = g_new0(gint, 1);
    *one = 1;
    while ((meta = GST_VIDEO_REGION_OF_INTEREST_META_ITERATE(buffer, &state))) {
        gint *roi_id = g_new0(gint, 1);
        *roi_id = meta->id;
        gpointer val = g_hash_table_lookup(hash_table, roi_id);
        if (val) {
            *(gint *)val += 1;
            g_hash_table_replace(hash_table, roi_id, val);
        } else {
            g_hash_table_insert(hash_table, roi_id, one);
        }
    }
    ck_assert(check_produced_roi_id(hash_table));
    g_hash_table_destroy(hash_table);
}

void test_gvadetect() {
    gchar command_line[8 * MAX_STR_PATH_SIZE];
    char detection_model_path[MAX_STR_PATH_SIZE];
    char video_file_path[MAX_STR_PATH_SIZE];
    const char *appsink_name = "appsink";

    ExitStatus status =
        get_model_path(detection_model_path, MAX_STR_PATH_SIZE, "yolov8_license_plate_detector", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    status = get_video_file_path(video_file_path, MAX_STR_PATH_SIZE, "Pexels_Videos_4786.mp4");
    ck_assert(status == EXIT_STATUS_SUCCESS);

    snprintf(
        command_line, sizeof(command_line),
        "filesrc location=%s ! identity eos-after=50 ! decodebin ! gvadetect model=%s ! appsink name=%s sync=false",
        video_file_path, detection_model_path, appsink_name);

    AppsinkTestData test_data = {
        .check_buf_cb = check_output_meta_region_id, .frame_count_limit = DEFAULT_FRAME_COUNT_LIMIT, .user_data = NULL};

    check_run_pipeline_with_appsink_default(command_line, GST_CLOCK_TIME_NONE, &appsink_name, 1, &test_data);
}

GST_START_TEST(test_region_id) {
    test_gvadetect();
}
GST_END_TEST;

static Suite *output_meta_test_suite(void) {
    Suite *s = suite_create("output_meta_test");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_compact_meta);
    tcase_add_test(tc_chain, test_region_id);

    return s;
}

GST_CHECK_MAIN(output_meta_test);
