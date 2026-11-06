/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gva_audio_base_inference.h"
#include "utils.h"
#include <sys/stat.h>

#define DEFAULT_MODEL NULL
#define DEFAULT_MODEL_PROC NULL
#define DEFAULT_SLIDING_WINDOW 1
#define DEFAULT_THRESHOLD 0.5
#define DEFAULT_DEVICE "CPU"

enum { PROP_0, PROP_MODEL, PROP_MODEL_PROC, PROP_SLIDING_WINDOW, PROP_THRESHOLD, PROP_DEVICE };

G_DEFINE_TYPE(GvaAudioBaseInference, gva_audio_base_inference, GST_TYPE_BASE_TRANSFORM);
static GstFlowReturn gva_audio_base_inference_transform_ip(GstBaseTransform *trans, GstBuffer *buf);
static gboolean gva_audio_base_inference_start(GstBaseTransform *trans);
static gboolean gva_audio_base_inference_stop(GstBaseTransform *trans);
static void gva_audio_base_inference_dispose(GObject *object);
static void gva_audio_base_inference_finalize(GObject *object);
static void gva_audio_base_inference_cleanup(GvaAudioBaseInference *);
static void gva_audio_base_inference_set_property(GObject *object, guint property_id, const GValue *value,
                                                  GParamSpec *pspec);
static void gva_audio_base_inference_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);

static void gva_audio_base_inference_init(GvaAudioBaseInference *audio_base_inference) {
    GST_DEBUG_OBJECT(audio_base_inference, "gva_audio_base_inference_init");

    if (audio_base_inference == NULL)
        return;

    gva_audio_base_inference_cleanup(audio_base_inference);
    audio_base_inference->model = g_strdup(DEFAULT_MODEL);
    audio_base_inference->model_proc = g_strdup(DEFAULT_MODEL_PROC);
    audio_base_inference->sliding_length = DEFAULT_SLIDING_WINDOW;
    audio_base_inference->threshold = DEFAULT_THRESHOLD;
    audio_base_inference->device = g_strdup(DEFAULT_DEVICE);
    audio_base_inference->values_checked = FALSE;
}

static void gva_audio_base_inference_class_init(GvaAudioBaseInferenceClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    gobject_class->set_property = gva_audio_base_inference_set_property;
    gobject_class->get_property = gva_audio_base_inference_get_property;
    gobject_class->dispose = gva_audio_base_inference_dispose;
    gobject_class->finalize = gva_audio_base_inference_finalize;
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gva_audio_base_inference_transform_ip);
    base_transform_class->start = GST_DEBUG_FUNCPTR(gva_audio_base_inference_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gva_audio_base_inference_stop);

    g_object_class_install_property(gobject_class, PROP_MODEL,
                                    g_param_spec_string("model", "Model", "Path to inference model network file",
                                                        DEFAULT_MODEL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(
        gobject_class, PROP_MODEL_PROC,
        g_param_spec_string("model-proc", "Model preproc and postproc",
                            "Path to JSON file with description of input/output layers pre-processing/post-processing",
                            DEFAULT_MODEL_PROC, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_SLIDING_WINDOW,
        g_param_spec_float("sliding-window", "Sliding window increment in seconds",
                           "Sliding window increment in seconds. Audio event detection is performed using a window of "
                           "1 second with an increment specified by the user. The default value of 1 implies no "
                           "overlap between successive inferences. An increment value of 0.5 implies inference "
                           "requests every 0.5 seconds with 0.5 seconds overlap",
                           0.1, 1, DEFAULT_SLIDING_WINDOW, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_THRESHOLD,
        g_param_spec_float("threshold", "Audio event detection Threshold",
                           "When model-proc contains only array of labels, event type with confidence value above the "
                           "threshold set here will be added to metadata",
                           0, 1, DEFAULT_THRESHOLD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_DEVICE,
        g_param_spec_string(
            "device", "Device",
            "Target device for inference. Please see OpenVINO™ Toolkit documentation for list of supported devices.",
            DEFAULT_DEVICE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

gboolean gva_audio_base_inference_stop(GstBaseTransform *trans) {
    GvaAudioBaseInference *audio_base_inference = GVA_AUDIO_BASE_INFERENCE(trans);

    GST_DEBUG_OBJECT(audio_base_inference, "stop");

    return TRUE;
}

gboolean gva_audio_base_inference_start(GstBaseTransform *trans) {
    GvaAudioBaseInference *audio_base_inference = GVA_AUDIO_BASE_INFERENCE(trans);
    GST_DEBUG_OBJECT(audio_base_inference, "start");

    GST_INFO_OBJECT(audio_base_inference,
                    "%s inference parameters:\n -- Model: %s\n -- Model proc: %s\n "
                    "-- Sliding window: %f\n -- Threshold: %f\n -- Device: %s\n",
                    GST_ELEMENT_NAME(GST_ELEMENT_CAST(audio_base_inference)), audio_base_inference->model,
                    audio_base_inference->model_proc, audio_base_inference->sliding_length,
                    audio_base_inference->threshold, audio_base_inference->device);

    if (audio_base_inference->model == NULL) {
        GST_ELEMENT_ERROR(audio_base_inference, RESOURCE, NOT_FOUND, ("'model' is not set"),
                          ("'model' property is not set"));
        return FALSE;
    } else if (!g_file_test(audio_base_inference->model, G_FILE_TEST_EXISTS)) {
        GST_ELEMENT_ERROR(audio_base_inference, RESOURCE, NOT_FOUND, ("'model' does not exist"),
                          ("path %s set in 'model' does not exist", audio_base_inference->model));
        return FALSE;
    } else {
        struct stat model_st;
        stat(audio_base_inference->model, &model_st);
        gulong mode_size = model_st.st_size;
        if (mode_size > MAX_MODEL_FILE_SIZE) {
            GST_ELEMENT_ERROR(audio_base_inference, RESOURCE, READ,
                              ("'model' %s file exceeds size limit", audio_base_inference->model),
                              ("maximum allowed size  %d (bytes) ", MAX_MODEL_FILE_SIZE));
            return FALSE;
        }
    }

    if (audio_base_inference->model_proc != NULL) {
        if (!g_file_test(audio_base_inference->model_proc, G_FILE_TEST_EXISTS)) {
            GST_ELEMENT_WARNING(audio_base_inference, RESOURCE, NOT_FOUND, ("'model-proc' does not exist"),
                                ("path %s set in 'model-proc' does not exist", audio_base_inference->model_proc));
        } else {
            struct stat proc_st;
            stat(audio_base_inference->model_proc, &proc_st);
            gulong proc_size = proc_st.st_size;
            if (proc_size > MAX_PROC_FILE_SIZE) {
                GST_ELEMENT_ERROR(audio_base_inference, RESOURCE, READ,
                                  ("'model-proc' %s JSON file exceeds size limit", audio_base_inference->model_proc),
                                  ("maximum allowed size  %d (bytes) ", MAX_PROC_FILE_SIZE));
                return FALSE;
            }
        }
    }

    if (!audio_base_inference->pre_proc) {
        GST_ELEMENT_ERROR(audio_base_inference, CORE, FAILED, ("Pre proc function missing"),
                          ("%s", "Unable to find Audio pre processing function"));
        return FALSE;
    }

    if (!audio_base_inference->post_proc) {
        GST_ELEMENT_ERROR(audio_base_inference, CORE, FAILED, ("Post proc function missing"),
                          ("%s", "Unable to find Audio Post processing function"));
        return FALSE;
    }

    if (!audio_base_inference->req_sample_size) {
        GST_ELEMENT_ERROR(audio_base_inference, CORE, FAILED, ("req_sample_size function missing"),
                          ("%s", "Unable to find Audio req_sample_size function"));
        return FALSE;
    }

    return create_handles(audio_base_inference);
}

void gva_audio_base_inference_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    GvaAudioBaseInference *audio_base_inference = GVA_AUDIO_BASE_INFERENCE(object);

    GST_DEBUG_OBJECT(audio_base_inference, "set_property");

    switch (property_id) {
    case PROP_MODEL:
        g_free(audio_base_inference->model);
        audio_base_inference->model = g_value_dup_string(value);
        break;
    case PROP_MODEL_PROC:
        g_free(audio_base_inference->model_proc);
        audio_base_inference->model_proc = g_value_dup_string(value);
        break;
    case PROP_SLIDING_WINDOW:
        audio_base_inference->sliding_length = g_value_get_float(value);
        break;
    case PROP_THRESHOLD:
        audio_base_inference->threshold = g_value_get_float(value);
        break;
    case PROP_DEVICE:
        g_free(audio_base_inference->device);
        audio_base_inference->device = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gva_audio_base_inference_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GvaAudioBaseInference *audio_base_inference = GVA_AUDIO_BASE_INFERENCE(object);

    GST_DEBUG_OBJECT(audio_base_inference, "get_property");

    switch (property_id) {
    case PROP_MODEL:
        g_value_set_string(value, audio_base_inference->model);
        break;
    case PROP_MODEL_PROC:
        g_value_set_string(value, audio_base_inference->model_proc);
        break;
    case PROP_SLIDING_WINDOW:
        g_value_set_float(value, audio_base_inference->sliding_length);
        break;
    case PROP_THRESHOLD:
        g_value_set_float(value, audio_base_inference->threshold);
        break;
    case PROP_DEVICE:
        g_value_set_string(value, audio_base_inference->device);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gva_audio_base_inference_dispose(GObject *object) {
    GvaAudioBaseInference *audio_base_inference = GVA_AUDIO_BASE_INFERENCE(object);

    GST_DEBUG_OBJECT(audio_base_inference, "dispose");

    G_OBJECT_CLASS(gva_audio_base_inference_parent_class)->dispose(object);
}

void gva_audio_base_inference_cleanup(GvaAudioBaseInference *audio_base_inference) {
    if (audio_base_inference == NULL)
        return;

    GST_DEBUG_OBJECT(audio_base_inference, "gva_audio_base_inference_cleanup");

    g_free(audio_base_inference->model);
    audio_base_inference->model = NULL;
    g_free(audio_base_inference->model_proc);
    audio_base_inference->model_proc = NULL;
    g_free(audio_base_inference->device);
    audio_base_inference->device = NULL;

    delete_handles(audio_base_inference);
}

void gva_audio_base_inference_finalize(GObject *object) {
    GvaAudioBaseInference *audio_base_inference = GVA_AUDIO_BASE_INFERENCE(object);

    GST_DEBUG_OBJECT(audio_base_inference, "finalize");
    gva_audio_base_inference_cleanup(audio_base_inference);

    G_OBJECT_CLASS(gva_audio_base_inference_parent_class)->finalize(object);
}

GstFlowReturn gva_audio_base_inference_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GvaAudioBaseInference *audio_base_inference = GVA_AUDIO_BASE_INFERENCE(trans);
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP(buf);
    GstClockTime start_time = gst_segment_to_stream_time(&trans->segment, GST_FORMAT_TIME, timestamp);

    return infer_audio(audio_base_inference, buf, start_time);
}
