/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvagenai.h"

#include <cmath>
#include <fstream>
#include <gst/video/video.h>
#include <memory>

#include "gva_caps.h"
#include "gva_json_meta.h"
#include <gst/analytics/gstanalyticsclassificationmtd.h>

#include "backends/genai_backend.hpp"

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
    PROP_PIPELINE_CONFIG,
    PROP_MODEL_CACHE_PATH,
    PROP_FRAME_RATE,
    PROP_CHUNK_SIZE,
    PROP_METRICS,
    PROP_BACKEND,
    PROP_HTTP_SERVER_URL,
    PROP_HTTP_API_KEY,
    PROP_HTTP_TIMEOUT,
    PROP_VISION_MODE
};

// How accumulated frames are presented to the VLM. Determines the native vision tag the
// pipeline injects into the prompt (image vs video) and which generate() input is used.
enum GstGvaGenAIVisionMode {
    GVAGENAI_VISION_MODE_IMAGE = 0, // frames sent as independent images (ov::genai::images)
    GVAGENAI_VISION_MODE_VIDEO = 1  // frames sent as one video clip (ov::genai::videos)
};

#define GST_TYPE_GVAGENAI_VISION_MODE (gst_gvagenai_vision_mode_get_type())
static GType gst_gvagenai_vision_mode_get_type(void) {
    static GType vision_mode_type = 0;
    if (g_once_init_enter(&vision_mode_type)) {
        static const GEnumValue modes[] = {
            {GVAGENAI_VISION_MODE_IMAGE, "Present accumulated frames as independent images", "image"},
            {GVAGENAI_VISION_MODE_VIDEO, "Present accumulated frames as one video clip", "video"},
            {0, NULL, NULL}};
        GType type = g_enum_register_static("GstGvaGenAIVisionMode", modes);
        g_once_init_leave(&vision_mode_type, type);
    }
    return vision_mode_type;
}

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

// Backend pointer stored in gvagenai->backend (heap-allocated shared_ptr)
using BackendPtr = std::shared_ptr<genai::IGenAIBackend>;

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
        g_param_spec_string("model-path", "Model Path",
                            "Path to the local GenAI model ('openvino' backend), or the model name to "
                            "request from the server ('openai-http' backend)",
                            NULL, G_PARAM_READWRITE));

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

    g_object_class_install_property(
        gobject_class, PROP_PIPELINE_CONFIG,
        g_param_spec_string(
            "pipeline-config", "Pipeline Config",
            "OpenVINO device properties passed to the pipeline at construction, as KEY=VALUE,KEY=VALUE format", NULL,
            G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_MODEL_CACHE_PATH,
                                    g_param_spec_string("model-cache-path", "Model Cache Path",
                                                        "Path for caching compiled models (GPU/NPU only)", "ov_cache",
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

    g_object_class_install_property(gobject_class, PROP_BACKEND,
                                    g_param_spec_string("backend", "Backend",
                                                        "Inference backend: 'openvino' (local) or "
                                                        "'openai-http' (remote OpenAI-compatible server)",
                                                        "openvino", G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_HTTP_SERVER_URL,
                                    g_param_spec_string("http-server-url", "HTTP Server URL",
                                                        "Base URL of the OpenAI-compatible server "
                                                        "(e.g. http://localhost:8000/v1)",
                                                        NULL, G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_HTTP_API_KEY,
                                    g_param_spec_string("http-api-key", "HTTP API Key",
                                                        "Optional Bearer token / API key for the HTTP server", NULL,
                                                        G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_HTTP_TIMEOUT,
                                    g_param_spec_string("http-timeout", "HTTP Timeout",
                                                        "Optional request timeout in milliseconds", NULL,
                                                        G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_VISION_MODE,
        g_param_spec_enum("vision-mode", "Vision Mode",
                          "How accumulated frames are presented to the model: as independent images, or as one "
                          "video clip. Video mode requires a video-capable model.",
                          GST_TYPE_GVAGENAI_VISION_MODE, GVAGENAI_VISION_MODE_IMAGE, G_PARAM_READWRITE));

    GST_DEBUG_CATEGORY_INIT(gst_gvagenai_debug, "gvagenai", 0, "OpenVINO™ GenAI Inference");
}

/* Initialize the instance */
static void gst_gvagenai_init(GstGvaGenAI *gvagenai) {
    gvagenai->config.backend = g_strdup("openvino");
    gvagenai->config.model = NULL;
    gvagenai->config.device = g_strdup("CPU");
    gvagenai->config.cache_path = g_strdup("ov_cache");
    gvagenai->config.generation_config = NULL;
    gvagenai->config.scheduler_config = NULL;
    gvagenai->config.pipeline_config = NULL;
    gvagenai->config.include_metrics = FALSE;
    gvagenai->config.server_url = NULL;
    gvagenai->config.api_key = NULL;
    gvagenai->config.timeout_ms = NULL;
    gvagenai->config.vision_mode = GVAGENAI_VISION_MODE_IMAGE; // Send frames as images by default

    gvagenai->prompt = NULL;
    gvagenai->prompt_path = NULL;
    gvagenai->frame_rate = 0.0; // Process all frames by default
    gvagenai->chunk_size = 1;   // Process one frame at a time by default
    gvagenai->frame_counter = 0;
    gvagenai->input_fps = 0.0; // Unknown until caps are set
    gvagenai->prompt_string = NULL;
    gvagenai->prompt_changed = FALSE;

    gvagenai->backend = NULL;
    gvagenai->last_result = NULL;
    gvagenai->last_confidence = -1.0f;
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
    gvagenai->prompt_string = NULL;
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
        g_free(gvagenai->config.device);
        gvagenai->config.device = g_value_dup_string(value);
        break;
    case PROP_MODEL_PATH:
        g_free(gvagenai->config.model);
        gvagenai->config.model = g_value_dup_string(value);
        break;
    case PROP_PROMPT:
        // Lock to synchronize prompt updates with transform function
        GST_OBJECT_LOCK(gvagenai);
        g_free(gvagenai->prompt);
        gvagenai->prompt = g_value_dup_string(value);
        gvagenai->prompt_changed = TRUE;
        GST_OBJECT_UNLOCK(gvagenai);
        break;
    case PROP_PROMPT_PATH:
        g_free(gvagenai->prompt_path);
        gvagenai->prompt_path = g_value_dup_string(value);
        break;
    case PROP_GENERATION_CONFIG:
        g_free(gvagenai->config.generation_config);
        gvagenai->config.generation_config = g_value_dup_string(value);
        break;
    case PROP_SCHEDULER_CONFIG:
        g_free(gvagenai->config.scheduler_config);
        gvagenai->config.scheduler_config = g_value_dup_string(value);
        break;
    case PROP_PIPELINE_CONFIG:
        g_free(gvagenai->config.pipeline_config);
        gvagenai->config.pipeline_config = g_value_dup_string(value);
        break;
    case PROP_MODEL_CACHE_PATH:
        g_free(gvagenai->config.cache_path);
        gvagenai->config.cache_path = g_value_dup_string(value);
        break;
    case PROP_FRAME_RATE:
        gvagenai->frame_rate = g_value_get_double(value);
        gvagenai->frame_counter = 0; // Reset counter when changing frame rate
        break;
    case PROP_CHUNK_SIZE:
        gvagenai->chunk_size = g_value_get_uint(value);
        break;
    case PROP_METRICS:
        gvagenai->config.include_metrics = g_value_get_boolean(value);
        break;
    case PROP_BACKEND:
        g_free(gvagenai->config.backend);
        gvagenai->config.backend = g_value_dup_string(value);
        break;
    case PROP_HTTP_SERVER_URL:
        g_free(gvagenai->config.server_url);
        gvagenai->config.server_url = g_value_dup_string(value);
        break;
    case PROP_HTTP_API_KEY:
        g_free(gvagenai->config.api_key);
        gvagenai->config.api_key = g_value_dup_string(value);
        break;
    case PROP_HTTP_TIMEOUT:
        g_free(gvagenai->config.timeout_ms);
        gvagenai->config.timeout_ms = g_value_dup_string(value);
        break;
    case PROP_VISION_MODE:
        gvagenai->config.vision_mode = g_value_get_enum(value);
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
        g_value_set_string(value, gvagenai->config.device);
        break;
    case PROP_MODEL_PATH:
        g_value_set_string(value, gvagenai->config.model);
        break;
    case PROP_PROMPT:
        g_value_set_string(value, gvagenai->prompt);
        break;
    case PROP_PROMPT_PATH:
        g_value_set_string(value, gvagenai->prompt_path);
        break;
    case PROP_GENERATION_CONFIG:
        g_value_set_string(value, gvagenai->config.generation_config);
        break;
    case PROP_SCHEDULER_CONFIG:
        g_value_set_string(value, gvagenai->config.scheduler_config);
        break;
    case PROP_PIPELINE_CONFIG:
        g_value_set_string(value, gvagenai->config.pipeline_config);
        break;
    case PROP_MODEL_CACHE_PATH:
        g_value_set_string(value, gvagenai->config.cache_path);
        break;
    case PROP_FRAME_RATE:
        g_value_set_double(value, gvagenai->frame_rate);
        break;
    case PROP_CHUNK_SIZE:
        g_value_set_uint(value, gvagenai->chunk_size);
        break;
    case PROP_METRICS:
        g_value_set_boolean(value, gvagenai->config.include_metrics);
        break;
    case PROP_BACKEND:
        g_value_set_string(value, gvagenai->config.backend);
        break;
    case PROP_HTTP_SERVER_URL:
        g_value_set_string(value, gvagenai->config.server_url);
        break;
    case PROP_HTTP_API_KEY:
        g_value_set_string(value, gvagenai->config.api_key);
        break;
    case PROP_HTTP_TIMEOUT:
        g_value_set_string(value, gvagenai->config.timeout_ms);
        break;
    case PROP_VISION_MODE:
        g_value_set_enum(value, gvagenai->config.vision_mode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gvagenai_finalize(GObject *object) {
    GstGvaGenAI *gvagenai = GST_GVAGENAI(object);

    g_free(gvagenai->config.backend);
    g_free(gvagenai->config.model);
    g_free(gvagenai->config.device);
    g_free(gvagenai->config.cache_path);
    g_free(gvagenai->config.generation_config);
    g_free(gvagenai->config.scheduler_config);
    g_free(gvagenai->config.pipeline_config);
    g_free(gvagenai->config.server_url);
    g_free(gvagenai->config.api_key);
    g_free(gvagenai->config.timeout_ms);

    g_free(gvagenai->prompt);
    g_free(gvagenai->prompt_path);

    // Clean up backend and cached state
    g_free(gvagenai->prompt_string);
    g_free(gvagenai->last_result);
    if (gvagenai->backend) {
        delete static_cast<BackendPtr *>(gvagenai->backend);
        gvagenai->backend = NULL;
    }

    G_OBJECT_CLASS(gst_gvagenai_parent_class)->finalize(object);
}

static gboolean gst_gvagenai_start(GstBaseTransform *base) {
    GstGvaGenAI *gvagenai = GST_GVAGENAI(base);

    const gchar *backend_type = gvagenai->config.backend ? gvagenai->config.backend : "openvino";
    gboolean is_http = (g_strcmp0(backend_type, "openai-http") == 0);
    gboolean is_openvino = (g_strcmp0(backend_type, "openvino") == 0);

    if (!is_http && !is_openvino) {
        GST_ELEMENT_ERROR(gvagenai, RESOURCE, SETTINGS, ("Invalid backend"),
                          ("Unknown backend '%s'. Valid values: 'openvino', 'openai-http'", backend_type));
        return FALSE;
    }

    if (!gvagenai->config.model) {
        GST_ELEMENT_ERROR(gvagenai, RESOURCE, SETTINGS, ("Model not specified"),
                          ("'model-path' property must be set (local model path for 'openvino', "
                           "or model name for 'openai-http')"));
        return FALSE;
    }

    if (is_http && !gvagenai->config.server_url) {
        GST_ELEMENT_ERROR(gvagenai, RESOURCE, SETTINGS, ("HTTP backend not configured"),
                          ("'http-server-url' must be set for the 'openai-http' backend"));
        return FALSE;
    }

    if (!load_effective_prompt(gvagenai)) {
        GST_ELEMENT_ERROR(gvagenai, RESOURCE, FAILED, ("Failed to load effective prompt"),
                          ("Could not load or validate prompt configuration"));
        return FALSE;
    }

    // Create backend through the process-wide registry using the element config
    try {
        BackendPtr backend = genai::GenAIBackendRegistry::instance().create_backend(gvagenai->config);
        gvagenai->backend = new BackendPtr(std::move(backend));
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(gvagenai, LIBRARY, INIT, ("Failed to initialize GenAI backend"), ("%s", e.what()));
        return FALSE;
    }

    return TRUE;
}

static gboolean gst_gvagenai_stop(GstBaseTransform *base) {
    GstGvaGenAI *gvagenai = GST_GVAGENAI(base);

    if (gvagenai->backend) {
        auto *backend_ptr = static_cast<BackendPtr *>(gvagenai->backend);
        if (*backend_ptr) {
            (*backend_ptr)->clear_frames();
        }
        delete backend_ptr;
        gvagenai->backend = NULL;
    }

    return TRUE;
}

static GstFlowReturn gst_gvagenai_transform_ip(GstBaseTransform *base, GstBuffer *buf) {
    GstGvaGenAI *gvagenai = GST_GVAGENAI(base);

    if (!gvagenai->backend) {
        GST_ELEMENT_ERROR(gvagenai, CORE, STATE_CHANGE, ("Backend not initialized"),
                          ("GenAI backend is not initialized, element may not have started properly"));
        return GST_FLOW_ERROR;
    }

    GST_OBJECT_LOCK(gvagenai);
    gboolean _success = TRUE;
    if (gvagenai->prompt_changed) {
        _success = load_effective_prompt(gvagenai);
        gvagenai->prompt_changed = FALSE;
    }
    GST_OBJECT_UNLOCK(gvagenai);
    if (!_success) {
        GST_ELEMENT_ERROR(gvagenai, RESOURCE, FAILED, ("Failed to load effective prompt"),
                          ("Could not load or validate prompt configuration"));
        return GST_FLOW_ERROR;
    }

    // Get video info from pad
    GstVideoInfo info;
    GstCaps *caps = gst_pad_get_current_caps(base->sinkpad);
    gst_video_info_from_caps(&info, caps);
    gst_caps_unref(caps);

    BackendPtr &backend = *static_cast<BackendPtr *>(gvagenai->backend);

    gvagenai->frame_counter++;

    // Calculate frame sampling based on frame_rate
    gboolean skip_frame = FALSE;
    if (gvagenai->frame_rate > 0) {
        gdouble input_fps = (gdouble)info.fps_n / (gdouble)info.fps_d;
        guint frames_to_skip = (guint)std::ceil(input_fps / gvagenai->frame_rate);

        if (frames_to_skip > 0 && (gvagenai->frame_counter % frames_to_skip != 0)) {
            GST_DEBUG_OBJECT(gvagenai, "Skipping frame %u based on frame rate %f", gvagenai->frame_counter,
                             gvagenai->frame_rate);
            skip_frame = TRUE;
        }
    }

    // Run inference only on non-skipped frames
    if (!skip_frame) {
        // Accumulate frame in the backend
        try {
            backend->add_frame(buf, &info);
        } catch (const std::exception &e) {
            GST_ELEMENT_ERROR(gvagenai, STREAM, FAILED, ("Failed to add frame to backend"), ("Error: %s", e.what()));
            return GST_FLOW_ERROR;
        }

        // Only process if we've accumulated enough frames
        if (backend->frame_count() >= gvagenai->chunk_size) {
            const gboolean as_video = (gvagenai->config.vision_mode == GVAGENAI_VISION_MODE_VIDEO);

            // Derive the effective fps of the frames actually accumulated, so VideoMetadata.fps
            // matches the frames the model receives. With frame-rate sampling active, the achieved
            // rate is input_fps / ceil(input_fps / frame_rate), not the requested frame_rate.
            gdouble effective_fps = 0.0;
            if (as_video && gvagenai->input_fps > 0.0) {
                if (gvagenai->frame_rate > 0.0) {
                    guint frames_to_skip = (guint)std::ceil(gvagenai->input_fps / gvagenai->frame_rate);
                    effective_fps = (frames_to_skip > 0) ? (gvagenai->input_fps / frames_to_skip) : gvagenai->input_fps;
                } else {
                    effective_fps = gvagenai->input_fps; // no sampling: all frames pass through
                }
            }

            genai::GenAIResult result;
            try {
                result =
                    backend->infer(gvagenai->prompt_string, as_video, (float)effective_fps, GST_BUFFER_TIMESTAMP(buf));
            } catch (const std::exception &e) {
                GST_ELEMENT_ERROR(gvagenai, STREAM, FAILED, ("Failed to run backend inference"),
                                  ("Error: %s", e.what()));
                return GST_FLOW_ERROR;
            }

            // Persist last result/confidence for watermark rendering on subsequent frames
            g_free(gvagenai->last_result);
            gvagenai->last_result = g_strdup(result.text.c_str());
            gvagenai->last_confidence = result.confidence;

            const GstMetaInfo *meta_info = gst_gva_json_meta_get_info();
            if (meta_info && gst_buffer_is_writable(buf)) {
                auto *json_meta = (GstGVAJSONMeta *)gst_buffer_add_meta(buf, meta_info, NULL);
                json_meta->message = g_strdup(result.raw_json.c_str());
                GST_INFO_OBJECT(gvagenai, "Added meta message: %s", json_meta->message);
            }
        } else {
            GST_DEBUG_OBJECT(gvagenai, "Added frame %u of %u", (guint)backend->frame_count(), gvagenai->chunk_size);
        }
    }

    // Emit GstAnalyticsClsMtd on EVERY frame so gvawatermark renders persistently.
    // Uses the last known result (persists across frames until the next inference completes).
    const gchar *last_result = gvagenai->last_result;
    if (last_result && last_result[0] != '\0' && gst_buffer_is_writable(buf)) {
        GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(buf);
        if (!rmeta) {
            rmeta = gst_buffer_add_analytics_relation_meta(buf);
        }
        if (rmeta) {
            GQuark label = g_quark_from_string(last_result);
            // Pass 0.0 when confidence is unavailable (greedy decoding) so gvawatermark
            // renders the label text without a confidence percentage.
            const float raw_cls_conf = gvagenai->last_confidence;
            gfloat cls_confidence = (raw_cls_conf >= 0.0f) ? raw_cls_conf : 0.0f;
            GstAnalyticsClsMtd cls_mtd = {0, nullptr};
            gst_analytics_relation_meta_add_cls_mtd(rmeta, 1, &cls_confidence, &label, &cls_mtd);
        }
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

    // Cache input stream fps (used to derive VideoMetadata.fps in video vision-mode).
    // fps_d == 0 or fps_n == 0 means unknown/variable rate (e.g. live source) -> leave 0.0.
    GstGvaGenAI *gvagenai = GST_GVAGENAI(base);
    if (info.fps_d > 0 && info.fps_n > 0) {
        gvagenai->input_fps = (gdouble)info.fps_n / (gdouble)info.fps_d;
    } else {
        gvagenai->input_fps = 0.0;
    }

    return TRUE;
}
