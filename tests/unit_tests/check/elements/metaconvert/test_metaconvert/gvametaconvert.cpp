/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gstgvametaconvert.h>

#include "test_common.h"

#include "glib.h"
#include "gst/analytics/analytics.h"
#include "gst/check/internal-check.h"
#include "gva_json_meta.h"
#include "region_of_interest.h"
#include "test_utils.h"

#include <gst/rtp/rtp.h>
#include <gst/video/video.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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
    Resolution resolution;
    GVADetection box;
    uint8_t buffer[8];
    bool ignore_detections;
    std::string add_tensor_data;
    bool add_ntp_meta;
};

#ifdef AUDIO
#include "gva_audio_event_meta.h"
#define AUDIO_CAPS_TEMPLATE_STRING "audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved"

static GstStaticPadTemplate audio_srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(AUDIO_CAPS_TEMPLATE_STRING));

static GstStaticPadTemplate audio_sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(AUDIO_CAPS_TEMPLATE_STRING));

struct TestAudioData {
    std::string label;
    guint64 start_time;
    guint64 end_time;
    gint label_id;
    gdouble confidence;
};

void setup_audio_inbuffer(GstBuffer *inbuffer, gpointer user_data) {
    TestAudioData *test_data = static_cast<TestAudioData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not TestData");
    GstStructure *detection =
        gst_structure_new("detection", "start_timestamp", G_TYPE_UINT64, test_data->start_time, "end_timestamp",
                          G_TYPE_UINT64, test_data->end_time, "label_id", G_TYPE_INT, test_data->label_id, "confidence",
                          G_TYPE_DOUBLE, test_data->confidence, NULL);
    GstStructure *other_struct =
        gst_structure_new("other_struct", "label", G_TYPE_STRING, "test_label", "model_name", G_TYPE_STRING,
                          "test_model_name", "confidence", G_TYPE_DOUBLE, 1.0, NULL);
    GstGVAAudioEventMeta *meta = gst_gva_buffer_add_audio_event_meta(inbuffer, test_data->label.c_str(),
                                                                     test_data->start_time, test_data->end_time);
    gst_gva_audio_event_meta_add_param(meta, detection);
    gst_gva_audio_event_meta_add_param(meta, other_struct);
}

void check_audio_outbuffer(GstBuffer *outbuffer, gpointer user_data) {
    TestData *test_data = static_cast<TestData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not TestData");
    GstGVAJSONMeta *meta = GST_GVA_JSON_META_GET(outbuffer);
    ck_assert_msg(meta != NULL, "No meta found");
    ck_assert_msg(meta->message != NULL, "No message in meta");
    std::string str_meta_message(meta->message);
    json json_message = json::parse(meta->message);

    ck_assert_msg(json_message["channels"] == 1, "Expected message to contain [channels] with value 1. Message: \n%s",
                  meta->message);
    ck_assert_msg(json_message["events"][0]["detection"]["confidence"] == 1.0,
                  "Expected message to contain [events][0][detection][confidence] with value 1.0. Message: \n%s",
                  meta->message);
    ck_assert_msg(strcmp(json_message["events"][0]["detection"].value("label", "default").c_str(), "Speech") == 0,
                  "Expected message to contain [events][0][detection][label] with value Speech. Message: \n%s",
                  meta->message);
    ck_assert_msg(json_message["events"][0]["detection"]["label_id"] == 53,
                  "Expected message to contain [events][0][detection][label_id] with value 53. Message: \n%s",
                  meta->message);
    ck_assert_msg(json_message["events"][0]["detection"]["segment"]["end_timestamp"] == 3200000000,
                  "Expected message to contain [events][0][detection][segment][end_timestamp] with value 3200000000. "
                  "Message: \n%s",
                  meta->message);
    ck_assert_msg(json_message["events"][0]["detection"]["segment"]["start_timestamp"] == 2200000000,
                  "Expected message to contain [events][0][detection][segment][start_timestamp] with value 2200000000. "
                  "Message: \n%s",
                  meta->message);
    ck_assert_msg(json_message["events"][0]["end_timestamp"] == 3200000000,
                  "Expected message to contain [events][0][end_timestamp] with value 3200000000. Message: \n%s",
                  meta->message);
    ck_assert_msg(json_message["events"][0]["start_timestamp"] == 2200000000,
                  "Expected message to contain [events][0][start_timestamp] with value 2200000000. Message: \n%s",
                  meta->message);
    ck_assert_msg(strcmp(json_message["events"][0].value("event_type", "default").c_str(), "Speech") == 0,
                  "Expected message to contain [events][0][event_type] with value Speech. Message: \n%s",
                  meta->message);
    ck_assert_msg(json_message["rate"] == 16000, "Expected message to contain [rate] with value 16000. Message: \n%s",
                  meta->message);
}

TestAudioData test_audio_data[] = {"Speech", 2200000000, 3200000000, 53, 1};

GST_START_TEST(test_metaconvert_audio) {
    g_print("Starting test: test_metaconvert_audio\n");
    std::vector<std::string> supported_fp = {"FP32"};
    for (int i = 0; i < G_N_ELEMENTS(test_audio_data); i++) {
        for (const auto &fp : supported_fp) {
            run_audio_test("gvametaconvert", AUDIO_CAPS_TEMPLATE_STRING, &audio_srctemplate, &audio_sinktemplate,
                           setup_audio_inbuffer, check_audio_outbuffer, &test_audio_data[i], NULL);
        }
    }
}

GST_END_TEST;
#endif

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(VIDEO_CAPS_TEMPLATE_STRING));

void setup_inbuffer(GstBuffer *inbuffer, gpointer user_data) {
    TestData *test_data = static_cast<TestData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not TestData");

    if (test_data->add_ntp_meta) {
        GstCaps *ntp_caps = gst_caps_new_empty_simple("timestamp/x-ntp");
        guint64 ntp_timestamp_ns = G_GUINT64_CONSTANT(1771854462174494742);
        gst_buffer_add_reference_timestamp_meta(inbuffer, ntp_caps, ntp_timestamp_ns, GST_CLOCK_TIME_NONE);
        gst_caps_unref(ntp_caps);
    }
    GstVideoInfo info;
    gst_video_info_set_format(&info, TEST_BUFFER_VIDEO_FORMAT, test_data->resolution.width,
                              test_data->resolution.height);
    gst_buffer_add_video_meta(inbuffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_INFO_FORMAT(&info),
                              GST_VIDEO_INFO_WIDTH(&info), GST_VIDEO_INFO_HEIGHT(&info));

    if (test_data->ignore_detections)
        return;
    GstStructure *s =
        gst_structure_new("detection", "confidence", G_TYPE_DOUBLE, test_data->box.confidence, "label_id", G_TYPE_INT,
                          test_data->box.label_id, "precision", G_TYPE_INT, 10, "x_min", G_TYPE_DOUBLE,
                          test_data->box.x_min, "x_max", G_TYPE_DOUBLE, test_data->box.x_max, "y_min", G_TYPE_DOUBLE,
                          test_data->box.y_min, "y_max", G_TYPE_DOUBLE, test_data->box.y_max, "model_name",
                          G_TYPE_STRING, "model_name", "layer_name", G_TYPE_STRING, "layer_name", NULL);
    GVariant *v = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, test_data->buffer, 8, 1);
    gsize n_elem;
    gst_structure_set(s, "data_buffer", G_TYPE_VARIANT, v, "data", G_TYPE_POINTER,
                      g_variant_get_fixed_array(v, &n_elem, 1), NULL);

    gint x = test_data->box.x_min * test_data->resolution.width;
    gint y = test_data->box.y_min * test_data->resolution.height;
    gint w = (test_data->box.x_max - test_data->box.x_min) * test_data->resolution.width;
    gint h = (test_data->box.y_max - test_data->box.y_min) * test_data->resolution.height;

    GstVideoRegionOfInterestMeta *meta = gst_buffer_add_video_region_of_interest_meta(inbuffer, NULL, x, y, w, h);
    gst_video_region_of_interest_meta_add_param(meta, s);

    GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(inbuffer);
    ck_assert_msg(relation_meta != NULL, "Failed to add relation meta to buffer");

    GstAnalyticsODMtd od_mtd;
    gboolean ret = gst_analytics_relation_meta_add_oriented_od_mtd(relation_meta, 0, x, y, w, h, 0.0,
                                                                   test_data->box.confidence, &od_mtd);
    ck_assert_msg(ret == TRUE, "Failed to add oriented od mtd to relation meta");

    meta->id = od_mtd.id;
}

void check_outbuffer(GstBuffer *outbuffer, gpointer user_data) {
    TestData *test_data = static_cast<TestData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not TestData");
    GstGVAJSONMeta *meta = GST_GVA_JSON_META_GET(outbuffer);
    ck_assert_msg(meta != NULL, "No meta found");
    ck_assert_msg(meta->message != NULL, "No message in meta");
    std::string str_meta_message(meta->message);
    json json_message = json::parse(meta->message);
    ck_assert_msg(strcmp(json_message["tags"].dump().c_str(), "{\"tag_key\":\"tag_val\"}") == 0,
                  "Message does not contain tags %s", meta->message);
    ck_assert_msg(strcmp(json_message.value("source", "default").c_str(), "test_src") == 0,
                  "Message does not contain source %s", meta->message);
    ck_assert_msg(strcmp(json_message["resolution"].dump().c_str(), "{\"height\":480,\"width\":640}") == 0,
                  "Message does not contain resolution %s", meta->message);
    ck_assert_msg(json_message["timestamp"] == 0, "Message does not contain timestamp %s", meta->message);

    if (test_data->add_ntp_meta) {
        ck_assert_msg(json_message.contains("rtp"), "RTP metadata missing from JSON output. Message: %s",
                      meta->message);
        ck_assert_msg(json_message["rtp"].contains("sender_ntp_unix_timestamp_ns"),
                      "NTP Unix timestamp missing from JSON output. Message: %s", meta->message);
        guint64 expected_unix_ns =
            G_GUINT64_CONSTANT(1771854462174494742) - (G_GUINT64_CONSTANT(2208988800) * 1000000000);
        guint64 actual_unix_ns = json_message["rtp"]["sender_ntp_unix_timestamp_ns"].get<guint64>();
        ck_assert_msg(actual_unix_ns == expected_unix_ns,
                      "NTP Unix timestamp mismatch: expected %" G_GUINT64_FORMAT ", got %" G_GUINT64_FORMAT,
                      expected_unix_ns, actual_unix_ns);
    }
    if (test_data->ignore_detections) {
        ck_assert_msg(str_meta_message.find("objects") == std::string::npos,
                      "message has detection data. message content %s", meta->message);
        ck_assert_msg(str_meta_message.find("tensor") == std::string::npos,
                      "message has tensor data. message content %s", meta->message);
    } else if (test_data->add_tensor_data.empty() || test_data->add_tensor_data == "all") {
        ck_assert_msg(str_meta_message.find("objects") != std::string::npos,
                      "message has no detection data. message content %s", meta->message);
        ck_assert_msg(str_meta_message.find("tensor") != std::string::npos,
                      "message has no tensor data. message content %s", meta->message);
    } else if (test_data->add_tensor_data == "tensor") {
        ck_assert_msg(str_meta_message.find("objects") == std::string::npos,
                      "message has detection data. message content %s", meta->message);
        ck_assert_msg(str_meta_message.find("tensor") != std::string::npos,
                      "message has no tensor data. message content %s", meta->message);
        ;
    } else if (test_data->add_tensor_data == "detection") {
        ck_assert_msg(str_meta_message.find("objects") != std::string::npos,
                      "message has no detection data. message content %s", meta->message);
        ck_assert_msg(str_meta_message.find("tensor") == std::string::npos,
                      "message has tensor data. message content %s", meta->message);
    }
}

TestData test_data[] = {
    {{640, 480}, {0.29375, 0.54375, 0.40625, 0.94167, 0.8, 0, 0}, {0x7c, 0x94, 0x06, 0x3f, 0x09, 0xd7, 0xf2, 0x3e}}};

GST_START_TEST(test_metaconvert_no_detections) {
    g_print("Starting test: test_metaconvert_no_detections\n");
    std::vector<std::string> supported_fp = {"FP32"};

    for (int i = 0; i < G_N_ELEMENTS(test_data); i++) {
        for (const auto &fp : supported_fp) {
            test_data[i].ignore_detections = true;
            test_data[i].add_ntp_meta = false;
            run_test("gvametaconvert", VIDEO_CAPS_TEMPLATE_STRING, test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, check_outbuffer, &test_data[i], "tags", "{\"tag_key\":\"tag_val\"}", "source",
                     "test_src", "add-empty-results", true, NULL);
        }
    }
}

GST_END_TEST;

GST_START_TEST(test_metaconvert_all) {
    g_print("Starting test: test_metaconvert_all\n");
    std::vector<std::string> supported_fp = {"FP32"};

    for (int i = 0; i < G_N_ELEMENTS(test_data); i++) {
        for (const auto &fp : supported_fp) {
            test_data[i].add_tensor_data = "all";
            test_data[i].add_ntp_meta = true;
            run_test("gvametaconvert", VIDEO_CAPS_TEMPLATE_STRING, test_data[i].resolution, &srctemplate, &sinktemplate,
                     setup_inbuffer, check_outbuffer, &test_data[i], "add-tensor-data", TRUE, "add-rtp-timestamp", TRUE,
                     "tags", "{\"tag_key\":\"tag_val\"}", "source", "test_src", NULL);
        }
    }
}

GST_END_TEST;

static Suite *metaconvert_suite(void) {
    Suite *s = suite_create("metaconvert");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_metaconvert_no_detections);
    tcase_add_test(tc_chain, test_metaconvert_all);
#ifdef AUDIO
    tcase_add_test(tc_chain, test_metaconvert_audio);
#endif

    return s;
}

GST_CHECK_MAIN(metaconvert);
