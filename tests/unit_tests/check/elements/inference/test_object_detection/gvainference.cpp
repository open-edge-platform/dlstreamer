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

#include <atomic>
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

/* ========================================================================= */
/*  inference-interval changed from another thread while frames are being    */
/*  processed                                                                */
/* ========================================================================= */

struct PropertyToggler {
    GstElement *element;
    std::atomic<bool> stop{false};
    std::atomic<unsigned> rounds{0};
};

static gpointer toggle_runtime_properties(gpointer data) {
    auto *toggler = static_cast<PropertyToggler *>(data);
    unsigned round = 0;
    while (!toggler->stop) {
        g_object_set(toggler->element, "inference-interval", 1u + round % 3, NULL);
        /* reading while the streaming thread works is part of what is checked */
        guint interval = 0;
        g_object_get(toggler->element, "inference-interval", &interval, NULL);
        toggler->rounds++;
        round++;
        g_usleep(200);
    }
    return nullptr;
}

GST_START_TEST(test_runtime_property_changes_under_load) {
    g_print("Starting test: test_runtime_property_changes_under_load\n");
    char model_path[MAX_STR_PATH_SIZE];
    ck_assert(get_model_path(model_path, MAX_STR_PATH_SIZE, "yolo11s", "FP32") == EXIT_STATUS_SUCCESS);

    gchar *description =
        g_strdup_printf("videotestsrc num-buffers=120 pattern=ball ! video/x-raw,width=640,height=480,framerate=30/1 ! "
                        "gvadetect name=detect model=%s device=CPU ! fakesink sync=false",
                        model_path);
    GError *error = NULL;
    GstElement *pipeline = gst_parse_launch(description, &error);
    g_free(description);
    ck_assert_msg(pipeline != NULL && error == NULL, "Cannot create the pipeline: %s", error ? error->message : "");

    PropertyToggler toggler;
    toggler.element = gst_bin_get_by_name(GST_BIN(pipeline), "detect");
    ck_assert(toggler.element != NULL);

    ck_assert(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    GThread *thread = g_thread_new("toggle-properties", toggle_runtime_properties, &toggler);

    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg =
        gst_bus_timed_pop_filtered(bus, 120 * GST_SECOND, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    toggler.stop = true;
    g_thread_join(thread);

    ck_assert_msg(msg != NULL, "The pipeline did not finish in time");
    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
        GError *err = NULL;
        gst_message_parse_error(msg, &err, NULL);
        ck_abort_msg("The pipeline failed while properties were changed: %s", err ? err->message : "");
    }
    ck_assert_msg(toggler.rounds > 0, "No property was changed while the pipeline was running");
    g_print("Properties were changed %u times while the pipeline was running\n", toggler.rounds.load());

    gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(toggler.element);
    gst_object_unref(pipeline);
}

GST_END_TEST;

static Suite *inference_suite(void) {
    Suite *s = suite_create("inference");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_set_timeout(tc_chain, 180);
    tcase_add_test(tc_chain, test_obj_detection_inference_cpu);
    tcase_add_test(tc_chain, test_obj_detection_inference_gpu);
    tcase_add_test(tc_chain, test_runtime_property_changes_under_load);

    return s;
}

GST_CHECK_MAIN(inference);
