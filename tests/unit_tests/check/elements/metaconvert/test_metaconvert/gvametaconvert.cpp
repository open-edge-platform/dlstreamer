/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <gstgvametaconvert.h>

#include <dlstreamer/gst/metadata/g3d_lidar_meta.h>
#include <dlstreamer/gst/metadata/gva_tensor_meta.h>

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

#include <cmath>
#include <vector>

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

namespace {

constexpr size_t kPointPillarsDetectionWidth = 9;
constexpr guint kPointPillarsFrameId = 17;
constexpr guint kPointPillarsPointCount = 1024;
constexpr guint kPointPillarsStreamId = 5;
constexpr GstClockTime kPointPillarsLidarParseTs = 11 * GST_MSECOND;
constexpr GstClockTime kPointPillarsInferenceTs = 13 * GST_MSECOND;
constexpr float kFloatTolerance = 1e-6f;

const std::vector<float> kPointPillarsDetections = {10.5f, -4.25f, -1.75f, 1.6f, 4.2f, 1.4f, 0.25f, 0.95f, 2.0f};

GValueArray *vector_to_gvalue_array(const std::vector<guint> &values) {
    GValueArray *array = g_value_array_new(values.size());
    for (guint value : values) {
        GValue item = G_VALUE_INIT;
        g_value_init(&item, G_TYPE_UINT);
        g_value_set_uint(&item, value);
        g_value_array_append(array, &item);
        g_value_unset(&item);
    }
    return array;
}

void copy_buffer_to_structure(GstStructure *structure, const void *buffer, size_t size) {
    GVariant *variant = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, buffer, size, 1);
    gsize n_elem = 0;
    gst_structure_set(structure, "data_buffer", G_TYPE_VARIANT, variant, "data", G_TYPE_POINTER,
                      g_variant_get_fixed_array(variant, &n_elem, 1), NULL);
}

void assert_close(double actual, double expected, const char *message) {
    ck_assert_msg(std::abs(actual - expected) < kFloatTolerance, "%s: expected %.6f, got %.6f", message, expected,
                  actual);
}

void assert_json_float(const json &jvalue, double expected, const char *message) {
    ck_assert_msg(jvalue.is_number(), "%s: expected numeric value", message);
    assert_close(jvalue.get<double>(), expected, message);
}

struct PointPillarsTestData {
    Resolution resolution;
    std::vector<float> detections;
    bool add_tensor_data;
};

struct DepthSummaryTestData {
    Resolution resolution;
    bool add_tensor_data;
};

constexpr double kDepthCenter = 2.5;
constexpr double kDepthMean = 3.0;
constexpr double kDepthMedian = 2.75;
constexpr double kDepthMin = 1.0;
constexpr double kDepthMax = 6.5;
constexpr double kDepthStdDev = 0.8;
constexpr gint kDepthValidCount = 42;
constexpr double kDepthValidRatio = 0.75;

void setup_pointpillars_inbuffer(GstBuffer *inbuffer, gpointer user_data) {
    PointPillarsTestData *test_data = static_cast<PointPillarsTestData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not PointPillarsTestData");

    GstVideoInfo info;
    gst_video_info_set_format(&info, TEST_BUFFER_VIDEO_FORMAT, test_data->resolution.width,
                              test_data->resolution.height);
    gst_buffer_add_video_meta(inbuffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_INFO_FORMAT(&info),
                              GST_VIDEO_INFO_WIDTH(&info), GST_VIDEO_INFO_HEIGHT(&info));

    LidarMeta *lidar_meta = add_lidar_meta(inbuffer, kPointPillarsPointCount, kPointPillarsFrameId,
                                           kPointPillarsLidarParseTs, kPointPillarsStreamId);
    ck_assert_msg(lidar_meta != NULL, "Failed to attach LidarMeta");
    lidar_meta->exit_g3dinference_timestamp = kPointPillarsInferenceTs;

    GstGVATensorMeta *tensor_meta = GST_GVA_TENSOR_META_ADD(inbuffer);
    ck_assert_msg(tensor_meta != NULL, "Failed to attach GstGVATensorMeta");

    gst_structure_set_name(tensor_meta->data, "detection");
    gst_structure_set(tensor_meta->data, "element_id", G_TYPE_STRING, "g3dinference", "model_name", G_TYPE_STRING,
                      "pointpillars", "layer_name", G_TYPE_STRING, "pointpillars_3d_detection", "format", G_TYPE_STRING,
                      "pointpillars_3d", "precision", G_TYPE_INT, GVA_PRECISION_FP32, "layout", G_TYPE_INT,
                      GVA_LAYOUT_NC, "rank", G_TYPE_INT, 2, NULL);

    GValueArray *dims = vector_to_gvalue_array(
        {static_cast<guint>(test_data->detections.size() / kPointPillarsDetectionWidth), kPointPillarsDetectionWidth});
    gst_structure_set_array(tensor_meta->data, "dims", dims);
    g_value_array_free(dims);

    copy_buffer_to_structure(tensor_meta->data, test_data->detections.data(),
                             test_data->detections.size() * sizeof(float));
}

void check_pointpillars_outbuffer(GstBuffer *outbuffer, gpointer user_data) {
    PointPillarsTestData *test_data = static_cast<PointPillarsTestData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not PointPillarsTestData");

    GstGVAJSONMeta *meta = GST_GVA_JSON_META_GET(outbuffer);
    ck_assert_msg(meta != NULL, "No meta found");
    ck_assert_msg(meta->message != NULL, "No message in meta");

    json json_message = json::parse(meta->message);
    ck_assert_msg(json_message.contains("lidar_frame"), "LiDAR JSON must contain lidar_frame. Message: %s",
                  meta->message);
    ck_assert_msg(!json_message.contains("resolution"), "LiDAR JSON should not contain video resolution. Message: %s",
                  meta->message);

    const json &lidar_frame = json_message["lidar_frame"];
    ck_assert_msg(lidar_frame["frame_id"] == kPointPillarsFrameId, "Unexpected frame_id. Message: %s", meta->message);
    ck_assert_msg(lidar_frame["stream_id"] == kPointPillarsStreamId, "Unexpected stream_id. Message: %s",
                  meta->message);
    ck_assert_msg(lidar_frame["point_count"] == kPointPillarsPointCount, "Unexpected point_count. Message: %s",
                  meta->message);
    ck_assert_msg(lidar_frame["exit_lidarparse_timestamp"] == kPointPillarsLidarParseTs,
                  "Unexpected exit_lidarparse_timestamp. Message: %s", meta->message);
    ck_assert_msg(lidar_frame["exit_g3dinference_timestamp"] == kPointPillarsInferenceTs,
                  "Unexpected exit_g3dinference_timestamp. Message: %s", meta->message);

    ck_assert_msg(json_message.contains("objects"), "LiDAR JSON must contain objects. Message: %s", meta->message);
    const json &objects = json_message["objects"];
    ck_assert_msg(objects.is_array() && objects.size() == 1, "Expected exactly one 3D object. Message: %s",
                  meta->message);

    const json &object = objects[0];
    ck_assert_msg(object.contains("bbox_3d"), "3D object must contain bbox_3d. Message: %s", meta->message);
    ck_assert_msg(!object.contains("detection"), "3D object must not use legacy detection schema. Message: %s",
                  meta->message);
    ck_assert_msg(object["model"]["type"] == "pointpillars", "Unexpected model type. Message: %s", meta->message);
    ck_assert_msg(object["label_id"] == 2, "Unexpected label_id. Message: %s", meta->message);
    assert_json_float(object["confidence"], 0.95, "Unexpected PointPillars confidence");

    const json &bbox = object["bbox_3d"];
    assert_json_float(bbox["x"], 10.5, "Unexpected bbox_3d.x");
    assert_json_float(bbox["y"], -4.25, "Unexpected bbox_3d.y");
    assert_json_float(bbox["z"], -1.75, "Unexpected bbox_3d.z");
    assert_json_float(bbox["w"], 1.6, "Unexpected bbox_3d.w");
    assert_json_float(bbox["l"], 4.2, "Unexpected bbox_3d.l");
    assert_json_float(bbox["h"], 1.4, "Unexpected bbox_3d.h");
    assert_json_float(bbox["theta"], 0.25, "Unexpected bbox_3d.theta");

    if (test_data->add_tensor_data) {
        ck_assert_msg(json_message.contains("tensors"),
                      "LiDAR JSON must contain tensors when add-tensor-data=true. Message: %s", meta->message);
        const json &tensors = json_message["tensors"];
        ck_assert_msg(tensors.is_array() && tensors.size() == 1, "Expected exactly one tensor. Message: %s",
                      meta->message);

        const json &tensor = tensors[0];
        ck_assert_msg(tensor["name"] == "detection", "Unexpected tensor name. Message: %s", meta->message);
        ck_assert_msg(tensor["model_name"] == "pointpillars", "Unexpected tensor model_name. Message: %s",
                      meta->message);
        ck_assert_msg(tensor["layer_name"] == "pointpillars_3d_detection", "Unexpected tensor layer_name. Message: %s",
                      meta->message);
        ck_assert_msg(tensor["format"] == "pointpillars_3d", "Unexpected tensor format. Message: %s", meta->message);
        ck_assert_msg(tensor["precision"] == "FP32", "Unexpected tensor precision. Message: %s", meta->message);
        ck_assert_msg(tensor["layout"] == "NC", "Unexpected tensor layout. Message: %s", meta->message);
        ck_assert_msg(tensor["dims"].is_array() && tensor["dims"].size() == 2, "Unexpected tensor dims. Message: %s",
                      meta->message);
        ck_assert_msg(tensor["dims"][0] == 1 && tensor["dims"][1] == 9, "Unexpected tensor dims values. Message: %s",
                      meta->message);
        ck_assert_msg(tensor["data"].is_array() && tensor["data"].size() == kPointPillarsDetectionWidth,
                      "Unexpected tensor data length. Message: %s", meta->message);
        for (size_t i = 0; i < test_data->detections.size(); ++i) {
            assert_json_float(tensor["data"][i], test_data->detections[i], "Unexpected PointPillars tensor data");
        }
    } else {
        ck_assert_msg(!json_message.contains("tensors"),
                      "LiDAR JSON must not contain tensors when add-tensor-data=false. Message: %s", meta->message);
    }
}

void setup_depth_summary_inbuffer(GstBuffer *inbuffer, gpointer user_data) {
    DepthSummaryTestData *test_data = static_cast<DepthSummaryTestData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not DepthSummaryTestData");

    GstVideoInfo info;
    gst_video_info_set_format(&info, TEST_BUFFER_VIDEO_FORMAT, test_data->resolution.width,
                              test_data->resolution.height);
    gst_buffer_add_video_meta(inbuffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_INFO_FORMAT(&info),
                              GST_VIDEO_INFO_WIDTH(&info), GST_VIDEO_INFO_HEIGHT(&info));

    GstGVATensorMeta *tensor_meta = GST_GVA_TENSOR_META_ADD(inbuffer);
    ck_assert_msg(tensor_meta != NULL, "Failed to attach GstGVATensorMeta");

    gst_structure_set_name(tensor_meta->data, "depth");
    gst_structure_set(tensor_meta->data, "attribute_name", G_TYPE_STRING, "depth", "model_name", G_TYPE_STRING,
                      "depthanything", "layer_name", G_TYPE_STRING, "depth_output", "format", G_TYPE_STRING,
                      "depth_summary", "type", G_TYPE_STRING, "depth_result", "label", G_TYPE_STRING, "depth",
                      "depth_center", G_TYPE_DOUBLE, kDepthCenter, "depth_mean", G_TYPE_DOUBLE, kDepthMean,
                      "depth_median", G_TYPE_DOUBLE, kDepthMedian, "depth_min", G_TYPE_DOUBLE, kDepthMin,
                      "depth_max", G_TYPE_DOUBLE, kDepthMax, "depth_stddev", G_TYPE_DOUBLE, kDepthStdDev,
                      "depth_valid_count", G_TYPE_INT, kDepthValidCount, "depth_valid_ratio", G_TYPE_DOUBLE,
                      kDepthValidRatio, NULL);
}

void check_depth_summary_outbuffer(GstBuffer *outbuffer, gpointer user_data) {
    DepthSummaryTestData *test_data = static_cast<DepthSummaryTestData *>(user_data);
    ck_assert_msg(test_data != NULL, "Passed data is not DepthSummaryTestData");

    GstGVAJSONMeta *meta = GST_GVA_JSON_META_GET(outbuffer);
    ck_assert_msg(meta != NULL, "No meta found");
    ck_assert_msg(meta->message != NULL, "No message in meta");

    json json_message = json::parse(meta->message);
    ck_assert_msg(json_message.contains("objects"), "Depth JSON must contain objects. Message: %s", meta->message);
    ck_assert_msg(json_message["objects"].is_array() && json_message["objects"].size() == 1,
                  "Depth JSON must contain exactly one full-frame object. Message: %s", meta->message);

    const json &object = json_message["objects"][0];
    ck_assert_msg(object.contains("depth"), "Depth JSON must contain depth object. Message: %s", meta->message);
    const json &depth = object["depth"];
    ck_assert_msg(depth["label"] == "depth", "Unexpected depth label. Message: %s", meta->message);
    ck_assert_msg(depth.contains("model") && depth["model"]["name"] == "depthanything",
                  "Unexpected depth model name. Message: %s", meta->message);
    ck_assert_msg(!depth.contains("depth_center"),
                  "Depth JSON must not include legacy custom depth summary fields. Message: %s", meta->message);

    ck_assert_msg(!object.contains("tensors"),
                  "Depth JSON must not duplicate interpreted depth summary under tensors. Message: %s",
                  meta->message);
}

void assert_legacy_detection_object(const json &object, const TestData *test_data, const char *message) {
    ck_assert_msg(object.contains("detection"), "%s", message);
    ck_assert_msg(object.contains("tensors"), "%s", message);
    ck_assert_msg(!object.contains("bbox_3d"), "%s", message);

    const json &detection = object["detection"];
    ck_assert_msg(detection.contains("bounding_box"), "%s", message);
    ck_assert_msg(!detection.contains("bbox_3d"), "%s", message);
    assert_json_float(detection["bounding_box"]["x_min"], test_data->box.x_min, "Unexpected legacy x_min");
    assert_json_float(detection["bounding_box"]["x_max"], test_data->box.x_max, "Unexpected legacy x_max");
    assert_json_float(detection["bounding_box"]["y_min"], test_data->box.y_min, "Unexpected legacy y_min");
    assert_json_float(detection["bounding_box"]["y_max"], test_data->box.y_max, "Unexpected legacy y_max");
    assert_json_float(detection["confidence"], test_data->box.confidence, "Unexpected legacy confidence");
    ck_assert_msg(detection["label_id"] == test_data->box.label_id, "%s", message);

    const json &tensors = object["tensors"];
    ck_assert_msg(tensors.is_array() && tensors.size() == 1, "%s", message);
    const json &tensor = tensors[0];
    ck_assert_msg(tensor["name"] == "detection", "%s", message);
    ck_assert_msg(tensor["model_name"] == "model_name", "%s", message);
    ck_assert_msg(tensor["layer_name"] == "layer_name", "%s", message);
    ck_assert_msg(tensor["precision"] == "FP32", "%s", message);
    assert_json_float(tensor["confidence"], test_data->box.confidence, "Unexpected legacy tensor confidence");
    ck_assert_msg(tensor["label_id"] == test_data->box.label_id, "%s", message);
    ck_assert_msg(tensor.contains("data") && tensor["data"].is_array() && tensor["data"].size() == 2, "%s", message);
    ck_assert_msg(!tensor.contains("format"), "%s", message);
}

} // namespace

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
        // NTP timestamp (NTP epoch + some time since 1900)
        guint64 ntp_timestamp_ns = G_GUINT64_CONSTANT(3980842262174494742);
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
            G_GUINT64_CONSTANT(3980842262174494742) - (G_GUINT64_CONSTANT(2208988800) * 1000000000);
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
        ck_assert_msg(json_message.contains("objects"), "Legacy JSON must contain objects. Message: %s", meta->message);
        ck_assert_msg(json_message["objects"].is_array() && json_message["objects"].size() == 1,
                      "Legacy JSON must contain exactly one object. Message: %s", meta->message);
        assert_legacy_detection_object(json_message["objects"][0], test_data,
                                       "Legacy metaconvert output format changed unexpectedly");
        ck_assert_msg(!json_message.contains("tensors"),
                      "Legacy ROI path should not create top-level tensors. Message: %s", meta->message);
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

PointPillarsTestData pointpillars_test_data = {{640, 480}, kPointPillarsDetections, true};
DepthSummaryTestData depth_summary_test_data = {{640, 480}, true};

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

GST_START_TEST(test_metaconvert_pointpillars_3d) {
    g_print("Starting test: test_metaconvert_pointpillars_3d\n");
    run_test("gvametaconvert", VIDEO_CAPS_TEMPLATE_STRING, pointpillars_test_data.resolution, &srctemplate,
             &sinktemplate, setup_pointpillars_inbuffer, check_pointpillars_outbuffer, &pointpillars_test_data,
             "add-tensor-data", TRUE, NULL);
}

GST_END_TEST;

GST_START_TEST(test_metaconvert_depth_summary) {
    g_print("Starting test: test_metaconvert_depth_summary\n");

    depth_summary_test_data.add_tensor_data = true;
    run_test("gvametaconvert", VIDEO_CAPS_TEMPLATE_STRING, depth_summary_test_data.resolution, &srctemplate,
             &sinktemplate, setup_depth_summary_inbuffer, check_depth_summary_outbuffer, &depth_summary_test_data,
             "add-tensor-data", TRUE, NULL);

    depth_summary_test_data.add_tensor_data = false;
    run_test("gvametaconvert", VIDEO_CAPS_TEMPLATE_STRING, depth_summary_test_data.resolution, &srctemplate,
             &sinktemplate, setup_depth_summary_inbuffer, check_depth_summary_outbuffer, &depth_summary_test_data,
             NULL);
}

GST_END_TEST;

static Suite *metaconvert_suite(void) {
    Suite *s = suite_create("metaconvert");
    TCase *tc_chain = tcase_create("general");

    suite_add_tcase(s, tc_chain);
    tcase_add_test(tc_chain, test_metaconvert_no_detections);
    tcase_add_test(tc_chain, test_metaconvert_all);
    tcase_add_test(tc_chain, test_metaconvert_pointpillars_3d);
    tcase_add_test(tc_chain, test_metaconvert_depth_summary);
#ifdef AUDIO
    tcase_add_test(tc_chain, test_metaconvert_audio);
#endif

    return s;
}

GST_CHECK_MAIN(metaconvert);
