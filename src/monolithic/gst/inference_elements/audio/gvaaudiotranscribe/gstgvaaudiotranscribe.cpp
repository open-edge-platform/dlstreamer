/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvaaudiotranscribe.h"
#include "gstgvawhisperasrhandler.h"
#include <fstream>
#include <gst/analytics/analytics.h>
#include <gst/analytics/gstanalyticsclassificationmtd.h>
#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <openvino/genai/whisper_pipeline.hpp>
#include <string>
#include <vector>

#define ELEMENT_LONG_NAME "Audio transcription using Whisper models with extensible handler interface"
#define ELEMENT_DESCRIPTION                                                                                            \
    "Performs speech recognition using OpenVINO Whisper models. Supports extensible handler interface for custom "     \
    "model implementations."
#define SAMPLE_RATE 16000

GST_DEBUG_CATEGORY_STATIC(gva_audio_transcribe_debug_category);
#define GST_CAT_DEFAULT gva_audio_transcribe_debug_category
#define GST_AUDIO_TRANSCRIBE_THRESHOLD_SEC 3

enum { PROP_0, PROP_MODEL_PATH, PROP_DEVICE, PROP_MODEL_TYPE };

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                                                   GST_STATIC_CAPS("audio/x-raw, "
                                                                                   "format=(string)S16LE, "
                                                                                   "rate=(int)16000, "
                                                                                   "channels=(int)1"));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                                                                  GST_STATIC_CAPS("audio/x-raw, "
                                                                                  "format=(string)S16LE, "
                                                                                  "rate=(int)16000, "
                                                                                  "channels=(int)1"));

G_DEFINE_TYPE_WITH_CODE(GvaAudioTranscribe, gst_gva_audio_transcribe, GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gva_audio_transcribe_debug_category, "gvaaudiotranscribe", 0,
                                                "debug category for gvaaudiotranscribe element"));

// Forward declarations of methods
static void gst_gva_audio_transcribe_set_property(GObject *object, guint prop_id, const GValue *value,
                                                  GParamSpec *pspec);
static void gst_gva_audio_transcribe_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_gva_audio_transcribe_finalize(GObject *object);
static gboolean gst_gva_audio_transcribe_start(GstBaseTransform *base);
static gboolean gst_gva_audio_transcribe_stop(GstBaseTransform *base);
static GstFlowReturn gst_gva_audio_transcribe_transform_ip(GstBaseTransform *base, GstBuffer *buf);

void gst_gva_audio_transcribe_class_init(GvaAudioTranscribeClass *gvaaudiotranscribe_class) {
    GstElementClass *element_class = GST_ELEMENT_CLASS(gvaaudiotranscribe_class);
    GObjectClass *gobject_class = G_OBJECT_CLASS(gvaaudiotranscribe_class);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(gvaaudiotranscribe_class);

    // Set virtual methods
    gobject_class->set_property = gst_gva_audio_transcribe_set_property;
    gobject_class->get_property = gst_gva_audio_transcribe_get_property;
    gobject_class->finalize = gst_gva_audio_transcribe_finalize;

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_gva_audio_transcribe_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_gva_audio_transcribe_stop);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gva_audio_transcribe_transform_ip);

    // Install properties
    g_object_class_install_property(
        gobject_class, PROP_MODEL_PATH,
        g_param_spec_string("model", "Model", "Path to the model directory", NULL, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_DEVICE,
        g_param_spec_string("device", "Device", "Device to use for inference (CPU, GPU)", "CPU", G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_MODEL_TYPE,
        g_param_spec_string("model_type", "Model_Type",
                            "model_type value to use whisper for inference: 'whisper' (supported).", "whisper",
                            G_PARAM_READWRITE));

    // Setup pad templates
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_factory));
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_factory));

    gst_element_class_set_static_metadata(element_class, ELEMENT_LONG_NAME, "Audio Transcription", ELEMENT_DESCRIPTION,
                                          "Intel Corporation");
}

void gst_gva_audio_transcribe_init(GvaAudioTranscribe *gvaaudiotranscribe) {
    GST_DEBUG_OBJECT(gvaaudiotranscribe, "gst_gva_audio_transcribe_init");

    // Initialize properties with default values
    gvaaudiotranscribe->model_path = NULL;
    gvaaudiotranscribe->device = g_strdup("CPU");
    gvaaudiotranscribe->model_type = g_strdup("whisper");

    // Initialize internal state
    gvaaudiotranscribe->handler = nullptr;
    gvaaudiotranscribe->audio_data = new std::vector<float>();
    gvaaudiotranscribe->mutex = new std::mutex();

    GST_DEBUG_OBJECT(gvaaudiotranscribe, "Element initialized");

    GST_DEBUG_OBJECT(gvaaudiotranscribe, "Initialized gvaaudiotranscribe");
}

static void gst_gva_audio_transcribe_set_property(GObject *object, guint prop_id, const GValue *value,
                                                  GParamSpec *pspec) {
    GvaAudioTranscribe *gvaaudiotranscribe = GVA_AUDIO_TRANSCRIBE(object);

    switch (prop_id) {
    case PROP_MODEL_PATH:
        g_free(gvaaudiotranscribe->model_path);
        gvaaudiotranscribe->model_path = g_value_dup_string(value);
        break;
    case PROP_DEVICE:
        g_free(gvaaudiotranscribe->device);
        gvaaudiotranscribe->device = g_value_dup_string(value);
        break;
    case PROP_MODEL_TYPE:
        g_free(gvaaudiotranscribe->model_type);
        gvaaudiotranscribe->model_type = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gva_audio_transcribe_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GvaAudioTranscribe *gvaaudiotranscribe = GVA_AUDIO_TRANSCRIBE(object);

    switch (prop_id) {
    case PROP_MODEL_PATH:
        g_value_set_string(value, gvaaudiotranscribe->model_path);
        break;
    case PROP_DEVICE:
        g_value_set_string(value, gvaaudiotranscribe->device);
        break;
    case PROP_MODEL_TYPE:
        g_value_set_string(value, gvaaudiotranscribe->model_type);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gva_audio_transcribe_finalize(GObject *object) {
    GvaAudioTranscribe *gvaaudiotranscribe = GVA_AUDIO_TRANSCRIBE(object);

    GST_DEBUG_OBJECT(gvaaudiotranscribe, "Finalizing");

    // Free string properties
    g_free(gvaaudiotranscribe->model_path);
    g_free(gvaaudiotranscribe->device);
    g_free(gvaaudiotranscribe->model_type);

    // Delete C++ objects
    if (gvaaudiotranscribe->handler) {
        gvaaudiotranscribe->handler->cleanup();
        delete gvaaudiotranscribe->handler;
        gvaaudiotranscribe->handler = nullptr;
    }
    delete static_cast<std::vector<float> *>(gvaaudiotranscribe->audio_data);
    delete static_cast<std::mutex *>(gvaaudiotranscribe->mutex);

    // Call parent finalize
    G_OBJECT_CLASS(gst_gva_audio_transcribe_parent_class)->finalize(object);
}

static gboolean gst_gva_audio_transcribe_start(GstBaseTransform *base) {
    GvaAudioTranscribe *gvaaudiotranscribe = GVA_AUDIO_TRANSCRIBE(base);

    if (!gvaaudiotranscribe->model_path) {
        GST_ERROR_OBJECT(gvaaudiotranscribe, "Model path not specified");
        return FALSE;
    }

    if (!gvaaudiotranscribe->model_type || gvaaudiotranscribe->model_type[0] == '\0') {
        GST_ERROR_OBJECT(gvaaudiotranscribe, "model_type property is required (currently supported: 'whisper')");
        return FALSE;
    }

    // Check for supported model types - currently only Whisper is implemented
    if (g_strcmp0(gvaaudiotranscribe->model_type, "whisper") == 0) {
        // Whisper is supported
        gvaaudiotranscribe->handler = new WhisperHandler();
    } else {
        // Provide helpful message for unsupported types
        GST_ERROR_OBJECT(
            gvaaudiotranscribe,
            "Model type '%s' is not currently supported. "
            "Currently supported: 'whisper'. "
            "Feel free to implement support for '%s' by extending the GvaAudioTranscribeHandler interface! "
            "See gstgvaaudiotranscribehandler.h for the extensible interface.",
            gvaaudiotranscribe->model_type, gvaaudiotranscribe->model_type);
        return FALSE;
    }

    GST_INFO_OBJECT(gvaaudiotranscribe, "Initializing %s handler with model '%s' on device '%s'",
                    gvaaudiotranscribe->model_type, gvaaudiotranscribe->model_path, gvaaudiotranscribe->device);

    try {

        const std::string language = "<|en|>"; // English language
        const std::string task = "transcribe"; // Transcription task
        const bool return_timestamps = false;  // Enable timestamps
        if (!gvaaudiotranscribe->handler->initialize(gvaaudiotranscribe->model_path, gvaaudiotranscribe->device,
                                                     language, task, return_timestamps)) {
            GST_ERROR_OBJECT(gvaaudiotranscribe, "Handler initialization returned false (no exception)");
            delete gvaaudiotranscribe->handler;
            gvaaudiotranscribe->handler = nullptr;
            return FALSE;
        }

        // Log handler info for debugging/monitoring
        auto info = gvaaudiotranscribe->handler->get_info();
        GST_INFO_OBJECT(gvaaudiotranscribe, "Handler initialized: type=%s, backend=%s, status=%s",
                        info["handler_type"].c_str(), info["backend"].c_str(), info["status"].c_str());

    } catch (const std::exception &e) {
        GST_ERROR_OBJECT(gvaaudiotranscribe, "Handler initialization failed: %s", e.what());
        delete gvaaudiotranscribe->handler;
        gvaaudiotranscribe->handler = nullptr;
        return FALSE;
    }
    return TRUE;
}

static gboolean gst_gva_audio_transcribe_stop(GstBaseTransform *base) {
    GvaAudioTranscribe *gvaaudiotranscribe = GVA_AUDIO_TRANSCRIBE(base);

    GST_DEBUG_OBJECT(gvaaudiotranscribe, "Stopping element");

    if (gvaaudiotranscribe->handler) {
        gvaaudiotranscribe->handler->cleanup();
        delete gvaaudiotranscribe->handler;
        gvaaudiotranscribe->handler = nullptr;
    }

    auto *audio_data = static_cast<std::vector<float> *>(gvaaudiotranscribe->audio_data);
    audio_data->clear();

    GST_DEBUG_OBJECT(gvaaudiotranscribe, "Element stopped successfully");
    return TRUE;
}

static GstFlowReturn gst_gva_audio_transcribe_transform_ip(GstBaseTransform *base, GstBuffer *buf) {
    GvaAudioTranscribe *gvaaudiotranscribe = GVA_AUDIO_TRANSCRIBE(base);
    auto *audio_data = static_cast<std::vector<float> *>(gvaaudiotranscribe->audio_data);
    auto *mutex = static_cast<std::mutex *>(gvaaudiotranscribe->mutex);

    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        GST_ERROR_OBJECT(gvaaudiotranscribe, "Failed to map buffer");
        return GST_FLOW_ERROR;
    }

    // Convert PCM int16 to float
    const int16_t *pcm_data = reinterpret_cast<const int16_t *>(map.data);
    size_t num_samples = map.size / sizeof(int16_t);
    {
        std::lock_guard<std::mutex> lock(*mutex);
        audio_data->reserve(audio_data->size() + num_samples);
        for (size_t i = 0; i < num_samples; ++i) {
            audio_data->push_back(static_cast<float>(pcm_data[i]) / 32768.0f);
        }
    }

    const size_t threshold_samples = SAMPLE_RATE * GST_AUDIO_TRANSCRIBE_THRESHOLD_SEC;
    if (audio_data->size() > threshold_samples) {
        GST_DEBUG_OBJECT(gvaaudiotranscribe, "Reached threshold of %zu samples, starting transcription",
                         threshold_samples);

        try {
            std::lock_guard<std::mutex> lock(*mutex);

            if (!gvaaudiotranscribe->handler) {
                GST_ERROR_OBJECT(gvaaudiotranscribe, "Handler not initialized");
                audio_data->clear();
                return GST_FLOW_ERROR;
            }
            TranscriptionResult result = gvaaudiotranscribe->handler->transcribe(*audio_data, buf);
            if (!result.text.empty()) {
                // Log transcript (visible with GST_DEBUG level >= INFO for this category)
                GST_INFO_OBJECT(gvaaudiotranscribe, "Transcript: %s (confidence: %.3f)", result.text.c_str(),
                                result.confidence);

                // Add GstAnalyticsClassification metadata to buffer
                if (gst_buffer_is_writable(buf)) {
                    // Get or create analytics relation meta
                    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buf);
                    if (!relation_meta) {
                        relation_meta = gst_buffer_add_analytics_relation_meta(buf);
                    }

                    if (relation_meta) {
                        // Create classification metadata with transcription result
                        GQuark transcript_quark = g_quark_from_string(result.text.c_str());
                        gfloat confidence_level = result.confidence; // Use actual confidence from Whisper model
                        GstAnalyticsClsMtd cls_mtd = {0, nullptr};

                        if (gst_analytics_relation_meta_add_cls_mtd(relation_meta, 1, &confidence_level,
                                                                    &transcript_quark, &cls_mtd)) {
                            GST_INFO_OBJECT(
                                gvaaudiotranscribe,
                                "Added transcription as GstAnalyticsClassification metadata (confidence: %.3f)",
                                confidence_level);

                            // Add model descriptor metadata
                            GQuark transcription_quark = g_quark_from_string("transcription");
                            gfloat descriptor_confidence = 0.0f;
                            GstAnalyticsClsMtd cls_descriptor_mtd = {0, nullptr};

                            if (gst_analytics_relation_meta_add_cls_mtd(relation_meta, 1, &descriptor_confidence,
                                                                        &transcription_quark, &cls_descriptor_mtd)) {
                                GST_INFO_OBJECT(gvaaudiotranscribe, "Added model descriptor metadata");

                                // Create relation between transcription result and model descriptor
                                if (gst_analytics_relation_meta_set_relation(relation_meta,
                                                                             GST_ANALYTICS_REL_TYPE_RELATE_TO,
                                                                             cls_mtd.id, cls_descriptor_mtd.id)) {
                                    GST_INFO_OBJECT(
                                        gvaaudiotranscribe,
                                        "Created relation between transcription result and model descriptor");
                                } else {
                                    GST_ERROR_OBJECT(
                                        gvaaudiotranscribe,
                                        "Failed to create relation between transcription result and model descriptor");
                                }
                            } else {
                                GST_ERROR_OBJECT(gvaaudiotranscribe, "Failed to add model descriptor metadata");
                            }
                        } else {
                            GST_ERROR_OBJECT(gvaaudiotranscribe, "Failed to add GstAnalyticsClassification metadata");
                        }
                    } else {
                        GST_ERROR_OBJECT(gvaaudiotranscribe, "Failed to get or create GstAnalyticsRelationMeta");
                    }
                }
            } else {
                GST_WARNING_OBJECT(gvaaudiotranscribe, "Transcription result is empty");
            }

            audio_data->clear();
        } catch (const std::exception &e) {
            GST_ERROR_OBJECT(gvaaudiotranscribe, "Error during transcription: %s", e.what());
            audio_data->clear();
        }
    }

    gst_buffer_unmap(buf, &map);
    return GST_FLOW_OK;
}
