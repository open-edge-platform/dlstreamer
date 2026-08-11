/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gva_utils.h"
#include "region_of_interest.h"
#include "tensor.h"
#include "test_common.h"
#include "test_utils.h"
#include "video_frame.h"

#include <gst/video/video.h>
#include <unordered_map>

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

typedef struct _GVADetection {
    gfloat x_min;
    gfloat y_min;
    gfloat x_max;
    gfloat y_max;
    gdouble confidence;
    gint label_id;
    gint object_id;
} GVADetection;

struct TestData {
    std::string image_file;
    std::string model_name;
    std::unordered_map<std::string, std::vector<std::string>> precision; // <hardware, array of FP>
    Resolution resolution;
    std::vector<GVADetection> boxes;
};

static char inference_name[] = "gvaclassify";
void setup_inbuffer(GstBuffer *inbuffer, gpointer user_data) {
    TestData *test_data = static_cast<TestData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not TestData");

    GstVideoInfo info;
    gst_video_info_set_format(&info, TEST_BUFFER_VIDEO_FORMAT, test_data->resolution.width,
                              test_data->resolution.height);
    gst_buffer_add_video_meta(inbuffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_INFO_FORMAT(&info),
                              GST_VIDEO_INFO_WIDTH(&info), GST_VIDEO_INFO_HEIGHT(&info));

    cv::Mat image = get_image(test_data->image_file, TEST_OCV_COLOR_CONVERT_CODE);
    gint image_size = image.cols * image.rows * image.channels();

    gst_buffer_fill(inbuffer, 0, image.data, image_size);
    for (auto input_bbox : test_data->boxes) {
        gst_buffer_add_video_region_of_interest_meta(
            inbuffer, NULL, input_bbox.x_min * test_data->resolution.width,
            input_bbox.y_min * test_data->resolution.height,
            (input_bbox.x_max - input_bbox.x_min) * test_data->resolution.width,
            (input_bbox.y_max - input_bbox.y_min) * test_data->resolution.height);
    }
}

void check_outbuffer(GstBuffer *outbuffer, gpointer user_data) {
    TestData *test_data = static_cast<TestData *>(user_data);
    GVA::VideoFrame video_frame(outbuffer);
    ck_assert_msg(video_frame.regions().size() == test_data->boxes.size(), "Not all objects have been classified");
    for (int i = 0; i < video_frame.regions().size(); i++) {
        ck_assert_msg(video_frame.regions()[i].tensors().size() >= 1, "The list of tensors is empty");
    }
}

TestData test_data[] = {{"classification_test_files/pedestrians.jpg",
                         "dima806_fairface_gender_image_detection",
                         {{"CPU", {"FP32"}}, {"GPU", {"FP32"}}},
                         {640, 480},
                         {}},
                        {"classification_test_files/pedestrians.jpg",
                         "dima806_fairface_gender_image_detection",
                         {{"CPU", {"FP32"}}, {"GPU", {"FP32"}}},
                         {640, 480},
                         {}}};

GST_START_TEST(test_classification_cpu) {
    g_print("Starting test: test_classification_cpu\n");
    char model_path[MAX_STR_PATH_SIZE];

    for (int i = 0; i < G_N_ELEMENTS(test_data); i++) {
        for (const auto &fp : test_data[i].precision["CPU"]) {
            g_print("Test: %d\tModel: %s\tPrecision: %s\n", i, test_data[i].model_name.c_str(), fp.c_str());
            ExitStatus status =
                get_model_path(model_path, MAX_STR_PATH_SIZE, test_data[i].model_name.c_str(), fp.c_str());
            ck_assert(status == EXIT_STATUS_SUCCESS);
            run_test("gvaclassify", VIDEO_CAPS_TEMPLATE_STRING, test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, check_outbuffer, &test_data[i], "model", model_path, "inference-region", 0, NULL);
        }
    }
}

GST_END_TEST;

GST_START_TEST(test_classification_gpu) {
    g_print("Starting test: test_classification_gpu\n");
    char model_path[MAX_STR_PATH_SIZE];

    for (int i = 0; i < G_N_ELEMENTS(test_data); i++) {
        for (const auto &fp : test_data[i].precision["GPU"]) {
            g_print("Test: %d\tModel: %s\tPrecision: %s\n", i, test_data[i].model_name.c_str(), fp.c_str());
            ExitStatus status =
                get_model_path(model_path, MAX_STR_PATH_SIZE, test_data[i].model_name.c_str(), fp.c_str());
            ck_assert(status == EXIT_STATUS_SUCCESS);
            run_test("gvaclassify", VIDEO_CAPS_TEMPLATE_STRING, test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, check_outbuffer, &test_data[i], "model", model_path, "inference-region", 0,
                     "device", "GPU", NULL);
        }
    }
}

GST_END_TEST;

GST_START_TEST(test_model_proc_property_json_does_not_match_schema) {
    g_print("Starting test: test_model_proc_property_json_does_not_match_schema\n");

    std::string prop_value = "classification_test_files/invalid_model_schema.json";

    char model_path[MAX_STR_PATH_SIZE];
    ExitStatus status =
        get_model_path(model_path, MAX_STR_PATH_SIZE, "dima806_fairface_gender_image_detection", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    check_bus_for_error("gvaclassify", &srctemplate, &sinktemplate, "", GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT,
                        "model", model_path, "model-proc", prop_value.c_str(), NULL);
}

GST_END_TEST;

static Suite *classification_suite(void) {
    Suite *s = suite_create("classification");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_classification_cpu);
    tcase_add_test(tc_chain, test_classification_gpu);
    tcase_add_test(tc_chain, test_model_proc_property_json_does_not_match_schema);

    return s;
}

GST_CHECK_MAIN(classification);
