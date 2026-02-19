/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "jsonconverter.h"
#include "gva_utils.h"
#include "video_frame.h"
#include <utils.h>

#ifdef AUDIO
#include "audioconverter.h"
#endif
#include "convert_tensor.h"
#include "g3d_radarprocess_meta.h"
#include "gva_json_meta.h"

#include <gst/analytics/analytics.h>
#include <gst/analytics/gstanalyticsclassificationmtd.h>
#include <gst/rtp/rtp.h>
#include <nlohmann/json.hpp>

#include <iomanip>
#include <iostream>

using json = nlohmann::json;

GST_DEBUG_CATEGORY_STATIC(gst_json_converter_debug);
#define GST_CAT_DEFAULT gst_json_converter_debug

namespace {

#define TIMESTAMP_LENGTH_BEFORE_MICROSECONDS 23
#define TIMESTAMP_OFFSET_POSITION 26
#define MICROSECONDS_TO_REMOVE 3

// Function to cut part of a string
gchar *cut_microseconds(const gchar *input) {
    if (input == NULL)
        return NULL;

    // Calculate the length of the new string
    size_t new_length = strlen(input) - MICROSECONDS_TO_REMOVE;

    // Allocate memory for the new string
    gchar *new_string = (gchar *)g_malloc(new_length + 1);
    if (new_string == NULL)
        return NULL;

    // Copy the part before the microseconds
    strncpy(new_string, input,
            TIMESTAMP_LENGTH_BEFORE_MICROSECONDS); // Copy up to the first three digits of the microseconds
    new_string[TIMESTAMP_LENGTH_BEFORE_MICROSECONDS] = '\0';

    // Append the time zone offset
    strcat(new_string, input + TIMESTAMP_OFFSET_POSITION);

    return new_string;
}

/**
 * @return JSON object which contains parameters such as resolution, timestamp, source and tags.
 */
json get_frame_data(GstGvaMetaConvert *converter, GstBuffer *buffer) {
    assert(converter && buffer && "Expected valid pointers GstGvaMetaConvert and GstBuffer");

    json res = json::object();
    GstSegment converter_segment = converter->base_gvametaconvert.segment;
    GstClockTime timestamp = gst_segment_to_stream_time(&converter_segment, GST_FORMAT_TIME, buffer->pts);

    GstVideoTimeCodeMeta *tc_meta = gst_buffer_get_video_time_code_meta(buffer);

    if (converter->info)
        res["resolution"] = json::object({{"width", converter->info->width}, {"height", converter->info->height}});
    if (converter->source)
        res["source"] = converter->source;
    if (timestamp != G_MAXUINT64)
        res["timestamp"] = timestamp;
    if (converter->tags && json::accept(converter->tags))
        res["tags"] = json::parse(converter->tags);
    if (tc_meta) {
        GstVideoTimeCode *vtc = gst_video_time_code_copy(&tc_meta->tc);
        GDateTime *frame_date_time = gst_video_time_code_to_date_time(vtc);

        // Format the datetime to ISO string with milliseconds
        gchar *iso_string = NULL;
        gchar *iso_string_millisec = NULL;
        GDateTime *utc_datetime = NULL;

        if (converter->timestamp_utc) {
            utc_datetime = g_date_time_to_utc(frame_date_time); // Convert the GDateTime object to UTC
            if (!utc_datetime)
                GST_WARNING("Failed to convert datetime to UTC");
            else {
                g_date_time_unref(frame_date_time);
                frame_date_time = utc_datetime;
                // UTC mode: add 'Z' at the end
                iso_string = g_date_time_format(frame_date_time, "%Y-%m-%dT%H:%M:%S.%fZ");
            }
        } else
            // Non-UTC mode: include offset from UTC
            iso_string = g_date_time_format(frame_date_time, "%Y-%m-%dT%H:%M:%S.%f:%z");

        if (iso_string == NULL)
            GST_WARNING("Failed to format the datetime to ISO string");
        else {

            if (!(converter->timestamp_microseconds)) {
                iso_string_millisec = cut_microseconds(iso_string);
                g_free(iso_string);
                iso_string = iso_string_millisec;
            }

            // Store the formatted timestamp in the result
            res["system_timestamp"] = iso_string;

            // Free the allocated resources
            g_free(iso_string);
        }

        if (frame_date_time)
            g_date_time_unref(frame_date_time);

        if (vtc)
            gst_video_time_code_free(vtc);
    }

    // Extract RTP timestamp from RTP buffer if available
    GstRTPBuffer rtpbuffer = GST_RTP_BUFFER_INIT;
    if (gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtpbuffer)) {
        guint32 rtp_timestamp = gst_rtp_buffer_get_timestamp(&rtpbuffer);
        guint32 rtp_ssrc = gst_rtp_buffer_get_ssrc(&rtpbuffer);
        guint8 rtp_seq = gst_rtp_buffer_get_seq(&rtpbuffer);

        json rtp_info = json::object();
        rtp_info["timestamp"] = rtp_timestamp;
        rtp_info["ssrc"] = rtp_ssrc;
        rtp_info["sequence"] = rtp_seq;

        res["rtp"] = rtp_info;

        gst_rtp_buffer_unmap(&rtpbuffer);
    }
    return res;
}

/**
 * @return JSON array which contains ROIs attributes and their detection results.
 * Also contains ROIs classification results if any.
 */
json convert_roi_detection(GstGvaMetaConvert *converter, GstBuffer *buffer) {
    assert(converter && buffer && "Expected valid pointers GstGvaMetaConvert and GstBuffer");

    json res = json::array();
    GVA::VideoFrame video_frame(buffer, converter->info);
    for (GVA::RegionOfInterest &roi : video_frame.regions()) {
        gint id = roi.object_id();

        json jobject = json::object();

        if (converter->add_tensor_data) {
            jobject["tensors"] = json::array();
        }

        auto rect = roi.rect();

        jobject.push_back({"x", rect.x});
        jobject.push_back({"y", rect.y});
        jobject.push_back({"w", rect.w});
        jobject.push_back({"h", rect.h});
        jobject.push_back({"region_id", roi.region_id()});

        gint parent_id = roi.parent_id();
        if (parent_id >= 0) {
            jobject.push_back({"parent_id", parent_id});
        }

        if (id != 0)
            jobject.push_back({"id", id});

        const std::string roi_type = roi.label();

        if (!roi_type.empty()) {
            jobject.push_back({"roi_type", roi_type});
        }
        for (GList *l = roi.get_params(); l; l = g_list_next(l)) {

            GstStructure *s = GST_STRUCTURE(l->data);
            const gchar *s_name = gst_structure_get_name(s);
            if (strcmp(s_name, "detection") == 0) {
                double xminval;
                double xmaxval;
                double yminval;
                double ymaxval;
                double confidence;
                int label_id;
                if (gst_structure_get(s, "x_min", G_TYPE_DOUBLE, &xminval, "x_max", G_TYPE_DOUBLE, &xmaxval, "y_min",
                                      G_TYPE_DOUBLE, &yminval, "y_max", G_TYPE_DOUBLE, &ymaxval, NULL)) {
                    json detection = json::object(
                        {{"bounding_box",
                          {{"x_min", xminval}, {"x_max", xmaxval}, {"y_min", yminval}, {"y_max", ymaxval}}}});

                    if (gst_structure_get(s, "confidence", G_TYPE_DOUBLE, &confidence, NULL)) {
                        detection.push_back({"confidence", confidence});
                    }

                    if (gst_structure_get(s, "label_id", G_TYPE_INT, &label_id, NULL)) {
                        detection.push_back({"label_id", label_id});
                    }

                    const std::string label = roi.label();

                    if (!label.empty()) {
                        detection.push_back({"label", label});
                    }
                    jobject.push_back(json::object_t::value_type("detection", detection));

                    // Handle extra_params_json if present
                    if (gst_structure_has_field(s, "extra_params_json")) {
                        const GValue *val = gst_structure_get_value(s, "extra_params_json");
                        if (G_VALUE_HOLDS_STRING(val)) {
                            const gchar *json_str = g_value_get_string(val);
                            if (json_str && strlen(json_str) > 0) {
                                try {
                                    jobject.push_back(
                                        json::object_t::value_type("extra_params", nlohmann::json::parse(json_str)));
                                } catch (const std::exception &e) {
                                    GST_WARNING("Failed to parse extra_params_json: %s", e.what());
                                    // Do not add the field if parsing fails
                                }
                            }
                        }
                    }
                }
            } else {
                char *label;
                char *model_name;
                if (gst_structure_get(s, "label", G_TYPE_STRING, &label, "model_name", G_TYPE_STRING, &model_name,
                                      NULL)) {
                    double confidence;
                    int label_id;
                    const gchar *attribute_name = gst_structure_has_field(s, "attribute_name")
                                                      ? gst_structure_get_string(s, "attribute_name")
                                                      : s_name;
                    json classification = json::object({{"label", label}, {"model", {{"name", model_name}}}});

                    if (gst_structure_get(s, "confidence", G_TYPE_DOUBLE, &confidence, NULL)) {
                        classification.push_back({"confidence", confidence});
                    }

                    if (gst_structure_get(s, "label_id", G_TYPE_INT, &label_id, NULL)) {
                        classification.push_back({"label_id", label_id});
                    }

                    jobject.push_back(json::object_t::value_type(attribute_name, classification));
                    g_free(label);
                    g_free(model_name);
                }
            }
            if (converter->add_tensor_data) {
                GVA::Tensor s_tensor = GVA::Tensor((GstStructure *)l->data);
                jobject["tensors"].push_back(convert_tensor(s_tensor));
            }
        }
        if (!jobject.empty()) {
            res.push_back(jobject);
        }
    }
    return res;
}

/**
 * @return JSON array which contains raw tensor metas from frame.
 */
json convert_frame_tensors(GstGvaMetaConvert *converter, GstBuffer *buffer) {
    assert(converter && buffer && "Expected valid pointers GstGvaMetaConvert and GstBuffer");

    GVA::VideoFrame video_frame(buffer, converter->info);
    const std::vector<GVA::Tensor> tensors = video_frame.tensors();
    json array = json::array();
    for (auto &tensor : video_frame.tensors()) {
        if (!tensor.has_field("type")) {
            array.push_back(convert_tensor(tensor));
        }
    }
    return array;
}

/**
 * @return JSON object which contains full-frame attributes and full-frame classification results from frame.
 */
json convert_frame_classification(GstGvaMetaConvert *converter, GstBuffer *buffer) {
    assert(converter && buffer && "Expected valid pointers GstGvaMetaConvert and GstBuffer");

    GVA::VideoFrame video_frame(buffer, converter->info);
    const std::vector<GVA::Tensor> tensors = video_frame.tensors();
    if (tensors.empty())
        return json{};

    json jobject = json::object();
    if (converter->add_tensor_data) {
        jobject["tensors"] = json::array();
    }
    jobject.push_back({"x", 0});
    jobject.push_back({"y", 0});
    jobject.push_back({"w", converter->info->width});
    jobject.push_back({"h", converter->info->height});

    for (GVA::Tensor &tensor : video_frame.tensors()) {
        if (tensor.has_field("label") || tensor.has_field("label_id")) {
            std::string label = tensor.label();
            std::string model_name = tensor.model_name();
            json classification = json::object({});
            if (!label.empty()) {
                classification.push_back(json::object_t::value_type("label", label));
            }
            if (!model_name.empty()) {
                classification.push_back(json::object_t::value_type("model", {{"name", model_name}}));
            }
            std::string attribute_name =
                tensor.has_field("attribute_name") ? tensor.get_string("attribute_name") : tensor.name();

            if (tensor.has_field("confidence")) {
                classification.push_back(json::object_t::value_type("confidence", tensor.confidence()));
            }
            if (tensor.has_field("label_id")) {
                classification.push_back(json::object_t::value_type("label_id", tensor.get_int("label_id")));
            }

            jobject.push_back(json::object_t::value_type(attribute_name, classification));
        }
        if (converter->add_tensor_data) {
            jobject["tensors"].push_back(convert_tensor(tensor));
        }
    }
    return jobject;
}

/**
 * @return JSON array which contains audio transcription classification metadata from buffer.
 * This function specifically filters for transcription metadata from gvaaudiotranscribe element.
 * It only processes classification metadata that:
 * 1. Is not related to specific ROIs (not part of object detection)
 * 2. Has a classification descriptor indicating it originates from gvaaudiotranscribe
 * This function should only be called from the audio processing path.
 */
json convert_audio_transcription_classification(GstGvaMetaConvert *converter, GstBuffer *buffer) {
    assert(converter && buffer && "Expected valid pointers GstGvaMetaConvert and GstBuffer");

    json res = json::array();

    // Get analytics relation metadata
    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
    if (!relation_meta) {
        return res; // No analytics metadata
    }

    // Helper lambda to check if a classification metadata is related to a transcription descriptor
    auto is_transcription_metadata = [&](GstAnalyticsMtd mtd) -> bool {
        // Check if this metadata has a RELATE_TO relationship with a transcription descriptor
        gpointer state = nullptr;
        GstAnalyticsClsMtd related_cls_mtd;

        while (gst_analytics_relation_meta_get_direct_related(relation_meta, mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
                                                              gst_analytics_cls_mtd_get_mtd_type(), &state,
                                                              &related_cls_mtd)) {
            gsize length = gst_analytics_cls_mtd_get_length(&related_cls_mtd);

            for (gsize i = 0; i < length; i++) {
                gfloat confidence = gst_analytics_cls_mtd_get_level(&related_cls_mtd, i);
                GQuark label_quark = gst_analytics_cls_mtd_get_quark(&related_cls_mtd, i);
                const gchar *label = g_quark_to_string(label_quark);

                // Check if this is a transcription descriptor (label="transcription", confidence=0.0)
                if (label && g_strcmp0(label, "transcription") == 0 && confidence < 1e-6f) {
                    return true;
                }
            }
        }
        return false;
    };

    // Helper lambda to check if a classification metadata is part of any ROI
    auto is_part_of_roi = [&](GstAnalyticsMtd mtd) -> bool {
        gpointer state = nullptr;
        GstAnalyticsODMtd related_od_mtd;

        // Check if this classification is part of any object detection (ROI)
        return gst_analytics_relation_meta_get_direct_related(relation_meta, mtd.id, GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                              gst_analytics_od_mtd_get_mtd_type(), &state,
                                                              &related_od_mtd);
    };

    // Iterate through all classification metadata
    gpointer state = nullptr;
    GstAnalyticsMtd mtd;
    while (gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_cls_mtd_get_mtd_type(), &mtd)) {
        GstAnalyticsClsMtd *cls_mtd = (GstAnalyticsClsMtd *)&mtd;
        gsize length = gst_analytics_cls_mtd_get_length(cls_mtd);

        // Check if this classification metadata should be included
        // Include only if: 1) not part of any ROI AND 2) related to transcription descriptor
        if (!is_part_of_roi(mtd) && is_transcription_metadata(mtd)) {
            for (gsize i = 0; i < length; i++) {
                gfloat confidence = gst_analytics_cls_mtd_get_level(cls_mtd, i);
                GQuark label_quark = gst_analytics_cls_mtd_get_quark(cls_mtd, i);
                const gchar *label = g_quark_to_string(label_quark);

                json classification = json::object();
                classification["label"] = label ? label : "";

                // Only include confidence for actual results (non-zero confidence)
                // Descriptors with 0.0 confidence are metadata markers - skip them
                const gfloat epsilon = 1e-6f;
                if (confidence > epsilon) {
                    classification["confidence"] = confidence;
                    res.push_back(classification);
                }
            }
        }
    }

    return res;
}

json convert_radar_process_meta(GstGvaMetaConvert *converter, GstBuffer *buffer) {
    UNUSED(converter);
    json result = json::object();

    // Find GstRadarProcessMeta
    gpointer state = NULL;
    GstMeta *meta;
    while ((meta = gst_buffer_iterate_meta_filtered(buffer, &state, GST_RADAR_PROCESS_META_API_TYPE))) {
        GstRadarProcessMeta *radar_meta = (GstRadarProcessMeta *)meta;

        result["frame_id"] = radar_meta->frame_id;
        result["timestamp"] = g_get_real_time();

        // Add point clouds
        json point_clouds_obj = json::object();
        point_clouds_obj["count"] = radar_meta->point_clouds_len;
        json points_array = json::array();
        for (gint i = 0; i < radar_meta->point_clouds_len; i++) {
            json point;
            point["range"] = radar_meta->ranges[i];
            point["speed"] = radar_meta->speeds[i];
            point["angle"] = radar_meta->angles[i];
            point["snr"] = radar_meta->snrs[i];
            points_array.push_back(point);
        }
        point_clouds_obj["points"] = points_array;
        result["point_clouds"] = point_clouds_obj;

        // Add clusters
        json clusters_obj = json::object();
        clusters_obj["count"] = radar_meta->num_clusters;
        json clusters_array = json::array();
        for (gint i = 0; i < radar_meta->num_clusters; i++) {
            json cluster;
            cluster["index"] = radar_meta->cluster_idx[i];
            cluster["center_x"] = radar_meta->cluster_cx[i];
            cluster["center_y"] = radar_meta->cluster_cy[i];
            cluster["radius_x"] = radar_meta->cluster_rx[i];
            cluster["radius_y"] = radar_meta->cluster_ry[i];
            cluster["avg_velocity"] = radar_meta->cluster_av[i];
            clusters_array.push_back(cluster);
        }
        clusters_obj["data"] = clusters_array;
        result["clusters"] = clusters_obj;

        // Add tracked objects
        json tracked_obj = json::object();
        tracked_obj["count"] = radar_meta->num_tracked_objects;
        json tracked_array = json::array();
        for (gint i = 0; i < radar_meta->num_tracked_objects; i++) {
            json tracker;
            tracker["id"] = radar_meta->tracker_ids[i];
            tracker["position_x"] = radar_meta->tracker_x[i];
            tracker["position_y"] = radar_meta->tracker_y[i];
            tracker["velocity_x"] = radar_meta->tracker_vx[i];
            tracker["velocity_y"] = radar_meta->tracker_vy[i];
            tracked_array.push_back(tracker);
        }
        tracked_obj["objects"] = tracked_array;
        result["tracked_objects"] = tracked_obj;

        // Only process the first radar meta
        break;
    }

    return result;
}

} // namespace

gboolean to_json(GstGvaMetaConvert *converter, GstBuffer *buffer) {
    GST_DEBUG_CATEGORY_INIT(gst_json_converter_debug, "jsonconverter", 0, "JSON converter");

    if (!converter) {
        GST_ERROR("Failed convert to json: GvaMetaConvert is null");
        return FALSE;
    }

    if (!buffer) {
        GST_ERROR_OBJECT(converter, "Failed convert to json: GstBuffer is null");
        return FALSE;
    }

    try {
        // Check for radar metadata first
        json radar_data = convert_radar_process_meta(converter, buffer);
        if (!radar_data.empty()) {
            std::string json_message = radar_data.dump(converter->json_indent);

            // Add as GVA JSON meta
            GstGVAJSONMeta *json_meta = GST_GVA_JSON_META_ADD(buffer);
            if (json_meta) {
                json_meta->message = g_strdup(json_message.c_str());
                GST_INFO_OBJECT(converter, "Radar JSON message: %s", json_message.c_str());
            } else {
                GST_ERROR_OBJECT(converter, "Failed to add GVA JSON meta for radar data");
            }
            return TRUE;
        }

        if (converter->info) {
            json jframe = get_frame_data(converter, buffer);
            /* objects section */
            json jframe_objects;
            json roi_detection = convert_roi_detection(converter, buffer);
            if (!roi_detection.empty()) {
                jframe_objects = roi_detection;
            } /* roi_detection can contain multiple objects, while frame_classification - only one */
            json frame_classification = convert_frame_classification(converter, buffer);
            if (!frame_classification.empty()) {
                jframe_objects.push_back(frame_classification);
            }

            /* tensors section */
            json jframe_tensors;
            if (converter->add_tensor_data) {
                jframe_tensors = convert_frame_tensors(converter, buffer);
            }

            if (jframe_objects.empty() && jframe_tensors.empty()) {
                if (!converter->add_empty_detection_results) {
                    GST_DEBUG_OBJECT(converter, "No detections found. Not posting JSON message");
                    return TRUE;
                }
            }

            if (!jframe.is_null()) {
                if (!jframe_objects.empty()) {
                    jframe["objects"] = jframe_objects;
                }
                if (!jframe_tensors.empty()) {
                    jframe["tensors"] = jframe_tensors;
                }
                std::string json_message = jframe.dump(converter->json_indent);
                GVA::VideoFrame video_frame(buffer, converter->info);
                video_frame.add_message(json_message);
                GST_INFO_OBJECT(converter, "JSON message: %s", json_message.c_str());
            }
        }
#ifdef AUDIO
        else {
            // For audio streams, handle transcription classification first, then fall back to traditional audio
            // metadata
            json audio_transcription_classification = convert_audio_transcription_classification(converter, buffer);
            if (!audio_transcription_classification.empty()) {
                // Create audio JSON message with analytics classification
                json audio_frame = json::object();
                GstSegment converter_segment = converter->base_gvametaconvert.segment;
                GstClockTime timestamp = gst_segment_to_stream_time(&converter_segment, GST_FORMAT_TIME, buffer->pts);

                if (converter->source)
                    audio_frame["source"] = converter->source;
                if (timestamp != G_MAXUINT64)
                    audio_frame["timestamp"] = timestamp;
                if (converter->tags && json::accept(converter->tags))
                    audio_frame["tags"] = json::parse(converter->tags);

                audio_frame["transcription"] = audio_transcription_classification;

                std::string json_message = audio_frame.dump(converter->json_indent);

                // Add as GVA JSON meta
                GstGVAJSONMeta *json_meta = GST_GVA_JSON_META_ADD(buffer);
                if (json_meta) {
                    json_meta->message = g_strdup(json_message.c_str());
                    GST_INFO_OBJECT(converter, "Audio JSON message: %s", json_message.c_str());
                } else {
                    GST_ERROR_OBJECT(converter, "Failed to add GVA JSON meta to audio buffer");
                }
                return TRUE;
            } else {
                // Fall back to traditional audio metadata conversion
                return convert_audio_meta_to_json(converter, buffer);
            }
        }
#endif
    } catch (const std::exception &e) {
        GST_ERROR_OBJECT(converter, "%s", Utils::createNestedErrorMsg(e).c_str());
        return FALSE;
    }
    return TRUE;
}