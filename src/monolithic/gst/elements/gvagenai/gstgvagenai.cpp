/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvagenai.h"

#include <fstream>
#include <gst/video/video.h>

#include "gva_caps.h"
#include "gva_json_meta.h"

#include "genai.hpp"

GST_DEBUG_CATEGORY(gst_gvagenai_debug);
#define GST_CAT_DEFAULT gst_gvagenai_debug

// Element property definitions
enum {
    PROP_0,
    PROP_DEVICE,
    PROP_MODEL_PATH,
    PROP_PROMPT,
    PROP_PROMPT_PATH,
    PROP_GENERATION_CONFIG,
    PROP_SCHEDULER_CONFIG,
    PROP_MODEL_CACHE_PATH,
    PROP_FRAME_RATE,
    PROP_CHUNK_SIZE,
    PROP_METRICS
};

// Pad templates
#define GVAGENAI_SYSTEM_MEM_CAPS GST_VIDEO_CAPS_MAKE("{ RGB, RGBA, RGBx, BGR, BGRA, BGRx, NV12, I420 }") "; "
#ifdef _WIN32
#define GVAGENAI_CAPS GVAGENAI_SYSTEM_MEM_CAPS D3D11MEMORY_CAPS
#else
#define GVAGENAI_CAPS GVAGENAI_SYSTEM_MEM_CAPS DMA_BUFFER_CAPS VAMEMORY_CAPS
#endif
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVAGENAI_CAPS));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVAGENAI_CAPS));

// Class initialization
G_DEFINE_TYPE(GstGvaGenAI, gst_gvagenai, GST_TYPE_BASE_TRANSFORM);

// GObject vmethod implementations
static void gst_gvagenai_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gvagenai_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_gvagenai_finalize(GObject *object);

// GstBaseTransform vmethod implementations
static gboolean gst_gvagenai_start(GstBaseTransform *base);
static gboolean gst_gvagenai_stop(GstBaseTransform *base);
static GstFlowReturn gst_gvagenai_transform_ip(GstBaseTransform *base, GstBuffer *buf);
static gboolean gst_gvagenai_set_caps(GstBaseTransform *base, GstCaps *incaps, GstCaps *outcaps);

// Utility functions
static gboolean load_effective_prompt(GstGvaGenAI *gvagenai);

// Initialize the element class
static void gst_gvagenai_class_init(GstGvaGenAIClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    // Setting up pads and setting metadata
    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    gst_element_class_set_static_metadata(element_class, "OpenVINO™ GenAI Inference", "Video/AI",
                                          "Runs OpenVINO™ GenAI inference on video frames", "Intel Corporation");

    // Set virtual methods
    gobject_class->set_property = gst_gvagenai_set_property;
    gobject_class->get_property = gst_gvagenai_get_property;
    gobject_class->finalize = gst_gvagenai_finalize;

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_gvagenai_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_gvagenai_stop);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gvagenai_transform_ip);
    base_transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_gvagenai_set_caps);

    // Install properties
    g_object_class_install_property(
        gobject_class, PROP_DEVICE,
        g_param_spec_string("device", "Device", "Device to use (CPU, GPU, NPU, etc.)", "CPU", G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_MODEL_PATH,
        g_param_spec_string("model-path", "Model Path", "Path to the GenAI model", NULL, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_PROMPT,
        g_param_spec_string("prompt", "Prompt", "Text prompt for the GenAI model", NULL, G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_PROMPT_PATH,
                                    g_param_spec_string("prompt-path", "Prompt Path",
                                                        "Path to text prompt file for the GenAI model", NULL,
                                                        G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_GENERATION_CONFIG,
                                    g_param_spec_string("generation-config", "Generation Config",
                                                        "Generation configuration as KEY=VALUE,KEY=VALUE format", NULL,
                                                        G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_SCHEDULER_CONFIG,
                                    g_param_spec_string("scheduler-config", "Scheduler Config",
                                                        "Scheduler configuration as KEY=VALUE,KEY=VALUE format", NULL,
                                                        G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_MODEL_CACHE_PATH,
                                    g_param_spec_string("model-cache-path", "Model Cache Path",
                                                        "Path for caching compiled models (GPU only)", "ov_cache",
                                                        G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_FRAME_RATE,
                                    g_param_spec_double("frame-rate", "Frame Rate",
                                                        "Number of frames sampled per second for inference "
                                                        "(0 = process all frames)",
                                                        0.0, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_CHUNK_SIZE,
                                    g_param_spec_uint("chunk-size", "Chunk Size", "Number of frames in one inference",
                                                      1, G_MAXUINT, 1, G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_METRICS,
                                    g_param_spec_boolean("metrics", "Metrics",
                                                         "Include performance metrics in JSON output", FALSE,
                                                         G_PARAM_READWRITE));

    GST_DEBUG_CATEGORY_INIT(gst_gvagenai_debug, "gvagenai", 0, "OpenVINO™ GenAI Inference");
}

/* Initialize the instance */
static void gst_gvagenai_init(GstGvaGenAI *gvagenai) {
    gvagenai->device = g_strdup("CPU");
    gvagenai->model_path = NULL;
    gvagenai->prompt = NULL;
    gvagenai->prompt_path = NULL;
    gvagenai->generation_config = NULL;
    gvagenai->scheduler_config = NULL;
    gvagenai->model_cache_path = g_strdup("ov_cache");
    gvagenai->frame_rate = 0.0; // Process all frames by default
    gvagenai->chunk_size = 1;   // Process one frame at a time by default
    gvagenai->metrics = FALSE;
    gvagenai->frame_counter = 0;
    gvagenai->prompt_string = NULL;
    gvagenai->openvino_context = NULL;
}

// Function to load effective prompt and set prompt_string
static gboolean load_effective_prompt(GstGvaGenAI *gvagenai) {
    // Validate prompt or prompt-path
    gboolean has_prompt = (gvagenai->prompt && strlen(gvagenai->prompt) > 0);
    gboolean has_prompt_path = (gvagenai->prompt_path && strlen(gvagenai->prompt_path) > 0);
    if (!has_prompt && !has_prompt_path) {
        GST_ELEMENT_ERROR(gvagenai, RESOURCE, SETTINGS, ("Prompt not specified"),
                          ("Either 'prompt' or 'prompt-path' property must be specified"));
        return FALSE;
    }
    if (has_prompt && has_prompt_path) {
        GST_ELEMENT_ERROR(gvagenai, RESOURCE, SETTINGS, ("Conflicting prompt properties"),
                          ("Both 'prompt' and 'prompt-path' properties are set. Please specify only one."));
        return FALSE;
    }

    g_free(gvagenai->prompt_string);
    if (has_prompt) {
        gvagenai->prompt_string = g_strdup(gvagenai->prompt);
    } else if (has_prompt_path) {
        try {
            std::ifstream file(gvagenai->prompt_path);
            if (!file.is_open()) {
                GST_ELEMENT_ERROR(gvagenai, RESOURCE, OPEN_READ, ("Failed to open prompt file"),
                                  ("Could not open file: %s", gvagenai->prompt_path));
                return FALSE;
            }

            auto content = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            if (content.empty()) {
                GST_WARNING_OBJECT(gvagenai, "Prompt file is empty: %s", gvagenai->prompt_path);
                return FALSE;
            }

            gvagenai->prompt_string = g_strdup(content.c_str());
        } catch (const std::exception &e) {
            GST_ELEMENT_ERROR(gvagenai, RESOURCE, READ, ("Error reading prompt file"),
                              ("Failed to read file %s: %s", gvagenai->prompt_path, e.what()));
            return FALSE;
        }
    } else {
        return FALSE;
    }

    GST_INFO_OBJECT(gvagenai, "Using prompt: %s", gvagenai->prompt_string);
    return TRUE;
}

static void gst_gvagenai_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaGenAI *gvagenai = GST_GVAGENAI(object);

    switch (prop_id) {
    case PROP_DEVICE:
        g_free(gvagenai->device);
        gvagenai->device = g_value_dup_string(value);
        break;
    case PROP_MODEL_PATH:
        g_free(gvagenai->model_path);
        gvagenai->model_path = g_value_dup_string(value);
        break;
    case PROP_PROMPT:
        g_free(gvagenai->prompt);
        gvagenai->prompt = g_value_dup_string(value);
        break;
    case PROP_PROMPT_PATH:
        g_free(gvagenai->prompt_path);
        gvagenai->prompt_path = g_value_dup_string(value);
        break;
    case PROP_GENERATION_CONFIG:
        g_free(gvagenai->generation_config);
        gvagenai->generation_config = g_value_dup_string(value);
        break;
    case PROP_SCHEDULER_CONFIG:
        g_free(gvagenai->scheduler_config);
        gvagenai->scheduler_config = g_value_dup_string(value);
        break;
    case PROP_MODEL_CACHE_PATH:
        g_free(gvagenai->model_cache_path);
        gvagenai->model_cache_path = g_value_dup_string(value);
        break;
    case PROP_FRAME_RATE:
        gvagenai->frame_rate = g_value_get_double(value);
        gvagenai->frame_counter = 0; // Reset counter when changing frame rate
        break;
    case PROP_CHUNK_SIZE:
        gvagenai->chunk_size = g_value_get_uint(value);
        break;
    case PROP_METRICS:
        gvagenai->metrics = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gvagenai_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaGenAI *gvagenai = GST_GVAGENAI(object);

    switch (prop_id) {
    case PROP_DEVICE:
        g_value_set_string(value, gvagenai->device);
        break;
    case PROP_MODEL_PATH:
        g_value_set_string(value, gvagenai->model_path);
        break;
    case PROP_PROMPT:
        g_value_set_string(value, gvagenai->prompt);
        break;
    case PROP_PROMPT_PATH:
        g_value_set_string(value, gvagenai->prompt_path);
        break;
    case PROP_GENERATION_CONFIG:
        g_value_set_string(value, gvagenai->generation_config);
        break;
    case PROP_SCHEDULER_CONFIG:
        g_value_set_string(value, gvagenai->scheduler_config);
        break;
    case PROP_MODEL_CACHE_PATH:
        g_value_set_string(value, gvagenai->model_cache_path);
        break;
    case PROP_FRAME_RATE:
        g_value_set_double(value, gvagenai->frame_rate);
        break;
    case PROP_CHUNK_SIZE:
        g_value_set_uint(value, gvagenai->chunk_size);
        break;
    case PROP_METRICS:
        g_value_set_boolean(value, gvagenai->metrics);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gvagenai_finalize(GObject *object) {
    GstGvaGenAI *gvagenai = GST_GVAGENAI(object);

    g_free(gvagenai->device);
    g_free(gvagenai->model_path);
    g_free(gvagenai->prompt);
    g_free(gvagenai->prompt_path);
    g_free(gvagenai->generation_config);
    g_free(gvagenai->scheduler_config);
    g_free(gvagenai->model_cache_path);

    // Clean up context
    g_free(gvagenai->prompt_string);
    if (gvagenai->openvino_context) {
        delete static_cast<genai::OpenVINOGenAIContext *>(gvagenai->openvino_context);
        gvagenai->openvino_context = NULL;
    }

    G_OBJECT_CLASS(gst_gvagenai_parent_class)->finalize(object);
}

static gboolean gst_gvagenai_start(GstBaseTransform *base) {
    GstGvaGenAI *gvagenai = GST_GVAGENAI(base);

    if (!gvagenai->model_path) {
        GST_ELEMENT_ERROR(gvagenai, RESOURCE, SETTINGS, ("Model path not specified"),
                          ("'model-path' property must be set before starting"));
        return FALSE;
    }

    if (!load_effective_prompt(gvagenai)) {
        GST_ELEMENT_ERROR(gvagenai, RESOURCE, FAILED, ("Failed to load effective prompt"),
                          ("Could not load or validate prompt configuration"));
        return FALSE;
    }

    // Create and initialize context
    try {
        auto context =
            new genai::OpenVINOGenAIContext(gvagenai->model_path, gvagenai->device,
                                            gvagenai->model_cache_path ? gvagenai->model_cache_path : "ov_cache",
                                            gvagenai->generation_config ? gvagenai->generation_config : "",
                                            gvagenai->scheduler_config ? gvagenai->scheduler_config : "");
        gvagenai->openvino_context = context;
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(gvagenai, LIBRARY, INIT, ("Failed to initialize OpenVINO™ GenAI context"), ("%s", e.what()));
        return FALSE;
    }

    return TRUE;
}

static gboolean gst_gvagenai_stop(GstBaseTransform *base) {
    GstGvaGenAI *gvagenai = GST_GVAGENAI(base);

    if (gvagenai->openvino_context) {
        auto *context = static_cast<genai::OpenVINOGenAIContext *>(gvagenai->openvino_context);
        context->clear_tensor_vector();
        delete context;
        gvagenai->openvino_context = NULL;
    }

    return TRUE;
}

static GstFlowReturn gst_gvagenai_transform_ip(GstBaseTransform *base, GstBuffer *buf) {
    GstGvaGenAI *gvagenai = GST_GVAGENAI(base);

    if (!gvagenai->openvino_context) {
        GST_ELEMENT_ERROR(gvagenai, CORE, STATE_CHANGE, ("Context not initialized"),
                          ("OpenVINO GenAI context is not initialized, element may not have started properly"));
        return GST_FLOW_ERROR;
    }

    // Get video info from pad
    GstVideoInfo info;
    GstCaps *caps = gst_pad_get_current_caps(base->sinkpad);
    gst_video_info_from_caps(&info, caps);
    gst_caps_unref(caps);

    gvagenai->frame_counter++;

    // Calculate frame sampling based on frame_rate
    if (gvagenai->frame_rate > 0) {
        gdouble input_fps = (gdouble)info.fps_n / (gdouble)info.fps_d;
        guint frames_to_skip = (guint)std::ceil(input_fps / gvagenai->frame_rate);

        // Skip frames if needed
        if (frames_to_skip > 0 && (gvagenai->frame_counter % frames_to_skip != 0)) {
            GST_DEBUG_OBJECT(gvagenai, "Skipping frame %u based on frame rate %f", gvagenai->frame_counter,
                             gvagenai->frame_rate);
            return GST_FLOW_OK;
        }
    }

    auto *context = static_cast<genai::OpenVINOGenAIContext *>(gvagenai->openvino_context);

    // Convert frame to tensor and add to vector
    try {
        context->add_tensor_to_vector(buf, &info);
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(gvagenai, STREAM, FAILED, ("Failed to add frame to tensor vector"), ("Error: %s", e.what()));
        return GST_FLOW_ERROR;
    }

    // Only process if we've accumulated enough tensors
    if (context->get_tensor_vector_size() >= gvagenai->chunk_size) {
        // Process tensor vector
        try {
            context->inference_tensor_vector(gvagenai->prompt_string);
        } catch (const std::exception &e) {
            GST_ELEMENT_ERROR(gvagenai, STREAM, FAILED, ("Failed to inference tensor vector"), ("Error: %s", e.what()));
            return GST_FLOW_ERROR;
        }

        // Add metadata with the result to the latest frame
        const GstMetaInfo *meta_info = gst_gva_json_meta_get_info();
        if (meta_info && gst_buffer_is_writable(buf)) {
            auto *json_meta = (GstGVAJSONMeta *)gst_buffer_add_meta(buf, meta_info, NULL);
            json_meta->message =
                g_strdup(context->create_json_metadata(GST_BUFFER_TIMESTAMP(buf), gvagenai->metrics).c_str());
            GST_INFO_OBJECT(gvagenai, "Added meta message: %s", json_meta->message);
        } else {
            GST_WARNING_OBJECT(gvagenai, "Buffer is not writable or failed to get meta info");
        }
    } else {
        GST_DEBUG_OBJECT(gvagenai, "Added tensor %u of %u", (guint)context->get_tensor_vector_size(),
                         gvagenai->chunk_size);
    }

    return GST_FLOW_OK;
}

static gboolean gst_gvagenai_set_caps(GstBaseTransform *base, GstCaps *incaps, GstCaps *outcaps) {
    // Validate that we can handle the input caps
    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, incaps)) {
        GST_ELEMENT_ERROR(base, STREAM, FORMAT, ("Failed to parse input caps"),
                          ("Could not extract video information from input capabilities"));
        return FALSE;
    }

    // Check if the format is supported
    GstVideoFormat format = GST_VIDEO_INFO_FORMAT(&info);
    if (format != GST_VIDEO_FORMAT_RGB && format != GST_VIDEO_FORMAT_RGBA && format != GST_VIDEO_FORMAT_RGBx &&
        format != GST_VIDEO_FORMAT_BGR && format != GST_VIDEO_FORMAT_BGRA && format != GST_VIDEO_FORMAT_BGRx &&
        format != GST_VIDEO_FORMAT_NV12 && format != GST_VIDEO_FORMAT_I420) {
        GST_ELEMENT_ERROR(
            base, STREAM, FORMAT, ("Unsupported video format"),
            ("Format %s is not supported. Supported formats: RGB, RGBA, RGBx, BGR, BGRA, BGRx, NV12, I420",
             gst_video_format_to_string(format)));
        return FALSE;
    }

    return TRUE;
}
