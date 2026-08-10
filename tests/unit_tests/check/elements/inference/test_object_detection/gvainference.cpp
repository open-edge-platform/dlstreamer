/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gva_utils.h"
#include "region_of_interest.h"
#include "test_common.h"
#include "test_utils.h"

#include <gst/video/video.h>

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

struct TestData {
    std::string image_file;
    std::string model_name;
    Resolution resolution;
};

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
}

void check_outbuffer(GstBuffer *outbuffer, gpointer user_data) {
    TestData *test_data = static_cast<TestData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not TestData");

    GstVideoRegionOfInterestMeta *meta = NULL;
    gpointer state = NULL;
    int count = 0;
    while ((meta = GST_VIDEO_REGION_OF_INTEREST_META_ITERATE(outbuffer, &state))) {
        g_print("Object detected: %dx%d+%d+%d\n", meta->x, meta->y, meta->w, meta->h);
        count++;
    }

    ck_assert_msg(count != 0, "No objects were detected");
}

TestData cpu_test_data[] = {{"inference_test_files/car_2.jpg", "yolo11s", {640, 480}},
                            {"inference_test_files/car_1.png", "yolo26s", {640, 480}},
                            {"inference_test_files/car_1.png", "yolo11s", {640, 480}},
                            {"inference_test_files/pedestrians.jpg", "yolo26s", {640, 480}},
                            {"inference_test_files/pedestrians.jpg", "yolo11s", {640, 480}},
                            {"inference_test_files/nasa.jpg", "centerface", {640, 480}}};

GST_START_TEST(test_obj_detection_inference_cpu) {
    g_print("Starting test: test_obj_detection_inference_cpu\n");
    std::vector<std::string> supported_fp = {"FP32"};
    char model_path[MAX_STR_PATH_SIZE];

    for (int i = 0; i < G_N_ELEMENTS(cpu_test_data); i++) {
        for (const auto &fp : supported_fp) {
            g_print("Test: %d  Model: %s\n", i, cpu_test_data[i].model_name.c_str());
            ExitStatus status =
                get_model_path(model_path, MAX_STR_PATH_SIZE, cpu_test_data[i].model_name.c_str(), fp.c_str());
            ck_assert(status == EXIT_STATUS_SUCCESS);
            run_test("gvadetect", VIDEO_CAPS_TEMPLATE_STRING, cpu_test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, check_outbuffer, &cpu_test_data[i], "model", model_path, NULL);
        }
    }
}

GST_END_TEST;

TestData gpu_test_data[] = {{"inference_test_files/car_2.jpg", "yolo11s", {640, 480}},
                            {"inference_test_files/car_1.png", "yolo11s", {640, 480}},
                            {"inference_test_files/pedestrians.jpg", "yolo26s", {640, 480}}};

GST_START_TEST(test_obj_detection_inference_gpu) {
    g_print("Starting test: test_obj_detection_inference_gpu\n");
    char model_path[MAX_STR_PATH_SIZE];

    for (int i = 0; i < G_N_ELEMENTS(gpu_test_data); i++) {
        std::vector<std::string> supported_fp = std::vector<std::string>{"FP16"};
        for (const auto &fp : supported_fp) {
            g_print("Test: %d  Model: %s\n", i, gpu_test_data[i].model_name.c_str());
            ExitStatus status =
                get_model_path(model_path, MAX_STR_PATH_SIZE, gpu_test_data[i].model_name.c_str(), fp.c_str());
            ck_assert(status == EXIT_STATUS_SUCCESS);
            run_test("gvadetect", VIDEO_CAPS_TEMPLATE_STRING, gpu_test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, check_outbuffer, &gpu_test_data[i], "model", model_path, "device", "GPU", NULL);
            // FIXME: remove redundant one
            // run_test("gvadetect", VIDEO_CAPS_TEMPLATE_STRING, gpu_test_data[i].resolution, &srctemplate,
            // &sinktemplate,
            //          setup_inbuffer, check_outbuffer, &gpu_test_data[i], "model", model_path, "device",
            //          GST_GVA_INFERENCE_GPU, NULL);
        }
    }
}

GST_END_TEST;

static Suite *inference_suite(void) {
    Suite *s = suite_create("inference");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_obj_detection_inference_cpu);
    tcase_add_test(tc_chain, test_obj_detection_inference_gpu);

    return s;
}

GST_CHECK_MAIN(inference);
