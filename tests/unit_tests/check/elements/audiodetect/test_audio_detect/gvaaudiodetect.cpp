/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "audio_frame.h"
#include "gva_audio_event_meta.h"
#include "gva_json_meta.h"
#include "test_common.h"
#include "test_utils.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved"));

static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS("audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved"));

struct AudioTestData {
    std::string audio_data;
    std::string model_name;
    std::string audio_type;
};

void setup_inbuffer(GstBuffer *inbuffer, gpointer user_data) {
    AudioTestData *test_data = static_cast<AudioTestData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not TestData");
    constexpr int size = 32000; // rate * (Bits Per Frame)
    guint8 audio_wav_data[size];
    get_audio_data(audio_wav_data, size, test_data->audio_data);
    gst_buffer_fill(inbuffer, 0, audio_wav_data, size);
}

void check_outbuffer(GstBuffer *outbuffer, gpointer user_data) {
    AudioTestData *test_data = static_cast<AudioTestData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not TestData");
    GstStaticCaps static_caps[1] = GST_STATIC_CAPS("audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved");
    GstCaps *caps = gst_static_caps_get(static_caps);
    GVA::AudioFrame audio_frame(outbuffer, caps);
    for (GVA::AudioEvent event : audio_frame.events()) {

        const gchar *event_type_detected = g_quark_to_string(event._meta()->event_type);
        const gchar *event_type_actual = test_data->audio_type.c_str();
        bool events_equal = strcmp(event_type_detected, event_type_actual) == 0;
        ck_assert_msg(events_equal, "Detected (%s) and Actual (%s) event types do not match", event_type_detected,
                      event_type_actual);
    }
}

AudioTestData test_data[] = {{"audio_test_files/CryingBaby.bin", "aclnet", "Crying baby"},
                             {"audio_test_files/4-90014-A-42.bin", "aclnet", "Siren"},
                             {"audio_test_files/4-125070-A-19.bin", "aclnet", "Thunderstorm"},
                             {"audio_test_files/4-125929-A-40.bin", "aclnet", "Helicopter"},
                             {"audio_test_files/4-199261-A-0.bin", "aclnet", "Dog"}};

GST_START_TEST(test_audio_detection) {
    g_print("\n\nStarting test: test_audio_detection\n");
    char model_path[MAX_STR_PATH_SIZE];
    char model_proc_path[MAX_STR_PATH_SIZE];

    for (int i = 0; i < G_N_ELEMENTS(test_data); i++) {
        ExitStatus status = get_model_path(model_path, MAX_STR_PATH_SIZE, "aclnet", "FP32");
        ck_assert(status == EXIT_STATUS_SUCCESS);
        g_print("Test: %d  Model: %s\n", i, model_path);
        status = get_model_proc_path(model_proc_path, MAX_STR_PATH_SIZE, test_data[i].model_name.c_str());
        ck_assert(status == EXIT_STATUS_SUCCESS);
        run_audio_test("gvaaudiodetect", "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved",
                       &srctemplate, &sinktemplate, setup_inbuffer, check_outbuffer, &test_data[i], "model", model_path,
                       "model_proc", model_proc_path, NULL);
    }
}

GST_END_TEST;

GST_START_TEST(test_audio_detection_no_model) {
    g_print("\n\nStarting test: test_audio_detection_no_model\n");
    char model_path[MAX_STR_PATH_SIZE];
    char model_proc_path[MAX_STR_PATH_SIZE];

    ExitStatus status = get_model_path(model_path, MAX_STR_PATH_SIZE, "aclnet", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    g_print("Test: Model: %s\n", model_path);
    status = get_model_proc_path(model_proc_path, MAX_STR_PATH_SIZE, "aclnet");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    run_audio_test_fail("gvaaudiodetect", "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved",
                        &srctemplate, &sinktemplate, "model_proc", model_proc_path, NULL);
}

GST_END_TEST;

GST_START_TEST(test_audio_detection_no_model_path) {
    g_print("\n\nStarting test: test_audio_detection_no_model_path\n");
    char model_path[] = "";
    char model_proc_path[MAX_STR_PATH_SIZE];
    g_print("Test: Model: %s\n", model_path);
    ExitStatus status = get_model_proc_path(model_proc_path, MAX_STR_PATH_SIZE, "aclnet");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    run_audio_test_fail("gvaaudiodetect", "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved",
                        &srctemplate, &sinktemplate, "model", model_path, "model_proc", model_proc_path, NULL);
}

GST_END_TEST;

GST_START_TEST(test_audio_detection_no_model_proc) {
    g_print("\n\nStarting test: test_audio_detection_no_model_proc\n");
    char model_path[MAX_STR_PATH_SIZE];
    char model_proc_path[MAX_STR_PATH_SIZE];

    ExitStatus status = get_model_path(model_path, MAX_STR_PATH_SIZE, "aclnet", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    g_print("Test: Model: %s\n", model_path);
    run_audio_test_fail("gvaaudiodetect", "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved",
                        &srctemplate, &sinktemplate, "model", model_path, NULL);
}

GST_END_TEST;

GST_START_TEST(test_audio_detection_no_model_proc_path) {
    g_print("\n\nStarting test: test_audio_detection_no_model_proc_path\n");
    char model_path[MAX_STR_PATH_SIZE];
    char model_proc_path[] = "";

    ExitStatus status = get_model_path(model_path, MAX_STR_PATH_SIZE, "aclnet", "FP32");
    ck_assert(status == EXIT_STATUS_SUCCESS);
    g_print("Test: Model: %s\n", model_path);

    run_audio_test_fail("gvaaudiodetect", "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved",
                        &srctemplate, &sinktemplate, "model", model_path, "model_proc", model_proc_path, NULL);
}

GST_END_TEST;

static Suite *inference_suite(void) {
    Suite *s = suite_create("inference");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_audio_detection);
    tcase_add_test(tc_chain, test_audio_detection_no_model);
    tcase_add_test(tc_chain, test_audio_detection_no_model_path);
    tcase_add_test(tc_chain, test_audio_detection_no_model_proc);
    tcase_add_test(tc_chain, test_audio_detection_no_model_proc_path);
    return s;
}

GST_CHECK_MAIN(inference);
