/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gva_base_inference.h"

#include "common/post_processor/post_processor_c.h"
#include "common/pre_processors.h"
#include "config.h"
#include "utils.h"

#include "dlstreamer/gst/context.h"
#include "dlstreamer/gst/frame.h"
#include "inference_backend/buffer_mapper.h"
#include "inference_impl.h"

#include "gva_base_inference_priv.hpp"
#include <memory>

#define DEFAULT_MODEL nullptr
#define DEFAULT_MODEL_INSTANCE_ID nullptr
#define DEFAULT_SCHEDULING_POLICY "throughput"
#define DEFAULT_MODEL_PROC nullptr
#define DEFAULT_DEVICE "CPU"
#define DEFAULT_PRE_PROC "" // empty = autoselection
#define DEFAULT_INFERENCE_REGION FULL_FRAME
#define DEFAULT_OBJECT_CLASS nullptr
#define DEFAULT_LABELS nullptr

#define DEFAULT_MIN_THRESHOLD 0.
#define DEFAULT_MAX_THRESHOLD 1.
#define DEFAULT_THRESHOLD 0.5

#define DEFAULT_MIN_INFERENCE_INTERVAL 1
#define DEFAULT_MAX_INFERENCE_INTERVAL UINT_MAX
#define DEFAULT_INFERENCE_INTERVAL 1
#define DEFAULT_FIRST_FRAME_NUM 0

#define DEFAULT_RESHAPE FALSE

#define DEFAULT_MIN_BATCH_SIZE 0
#define DEFAULT_MAX_BATCH_SIZE 1024
#define DEFAULT_BATCH_SIZE 0

#define DEFAULT_MIN_RESHAPE_WIDTH 0
#define DEFAULT_MAX_RESHAPE_WIDTH UINT_MAX
#define DEFAULT_RESHAPE_WIDTH 0

#define DEFAULT_MIN_RESHAPE_HEIGHT 0
#define DEFAULT_MAX_RESHAPE_HEIGHT UINT_MAX
#define DEFAULT_RESHAPE_HEIGHT 0

#define DEFAULT_NO_BLOCK FALSE

#define DEFAULT_MIN_NIREQ 0
#define DEFAULT_MAX_NIREQ 1024
#define DEFAULT_NIREQ 0

#define DEFAULT_CPU_THROUGHPUT_STREAMS 0
#define DEFAULT_MIN_CPU_THROUGHPUT_STREAMS 0
#define DEFAULT_MAX_CPU_THROUGHPUT_STREAMS UINT_MAX

#define DEFAULT_GPU_THROUGHPUT_STREAMS 0
#define DEFAULT_MIN_GPU_THROUGHPUT_STREAMS 0
#define DEFAULT_MAX_GPU_THROUGHPUT_STREAMS UINT_MAX

#define DEFAULT_ALLOCATOR_NAME nullptr

#define DEFAULT_CUSTOM_PREPROC_LIB nullptr

#define DEFAULT_CUSTOM_POSTPROC_LIB nullptr

#define DEFAULT_OV_EXTENSION_LIB nullptr

#define DEFAULT_SHARE_VADISPLAY_CTX TRUE

G_DEFINE_TYPE_WITH_PRIVATE(GvaBaseInference, gva_base_inference, GST_TYPE_BASE_TRANSFORM);

GST_DEBUG_CATEGORY_STATIC(gva_base_inference_debug_category);
#define GST_CAT_DEFAULT gva_base_inference_debug_category

extern std::shared_ptr<InferenceImpl> acquire_inference_instance(GvaBaseInference *base_inference);

enum {
    PROP_0,
    PROP_MODEL,
    PROP_DEVICE,
    PROP_INFERENCE_INTERVAL,
    PROP_RESHAPE,
    PROP_BATCH_SIZE,
    PROP_RESHAPE_WIDTH,
    PROP_RESHAPE_HEIGHT,
    PROP_NO_BLOCK,
    PROP_NIREQ,
    PROP_MODEL_INSTANCE_ID,
    PROP_SCHEDULING_POLICY,
    PROP_PRE_PROC_BACKEND,
    PROP_MODEL_PROC,
    PROP_CPU_THROUGHPUT_STREAMS,
    PROP_GPU_THROUGHPUT_STREAMS,
    PROP_IE_CONFIG,
    PROP_PRE_PROC_CONFIG,
    PROP_INFERENCE_REGION,
    PROP_OBJECT_CLASS,
    PROP_LABELS,
    PROP_LABELS_FILE,
    PROP_SCALE_METHOD,
    PROP_CUSTOM_PREPROC_LIB,
    PROP_CUSTOM_POSTPROC_LIB,
    PROP_OV_EXTENSION_LIB,
    PROP_SHARE_VADISPLAY_CTX
};

GType gst_gva_base_inference_get_inf_region(void) {
    static GType gva_inference_region = 0;
    static const GEnumValue inference_region_types[] = {{FULL_FRAME, "Perform inference for full frame", "full-frame"},
                                                        {ROI_LIST, "Perform inference for roi list", "roi-list"},
                                                        {0, nullptr, nullptr}};

    if (!gva_inference_region) {
        gva_inference_region = g_enum_register_static("InferenceRegionType3", inference_region_types);
    }
    return gva_inference_region;
}

static void gva_base_inference_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gva_base_inference_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gva_base_inference_dispose(GObject *object);
static void gva_base_inference_finalize(GObject *object);

static gboolean gva_base_inference_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);
static gboolean gva_base_inference_check_properties_correctness(GvaBaseInference *base_inference);
static gboolean gva_base_inference_start(GstBaseTransform *trans);
static gboolean gva_base_inference_stop(GstBaseTransform *trans);
static gboolean gva_base_inference_sink_event(GstBaseTransform *trans, GstEvent *event);
static gboolean gva_base_inference_propose_allocation(GstBaseTransform *trans, GstQuery *decide_query, GstQuery *query);

static void on_base_inference_initialized(GvaBaseInference *base_inference);
static void gva_base_inference_update_object_classes(GvaBaseInference *base_inference);

static GstFlowReturn gva_base_inference_transform_ip(GstBaseTransform *trans, GstBuffer *buf);
static void gva_base_inference_cleanup(GvaBaseInference *base_inference);
static GstStateChangeReturn gva_base_inference_change_state(GstElement *element, GstStateChange transition);

static void gva_base_inference_init(GvaBaseInference *base_inference);

static void gva_base_inference_class_init(GvaBaseInferenceClass *klass);

static bool is_roi_inference_needed(GvaBaseInference *gva_base_inference, guint64 current_num_frame, GstBuffer *buffer,
                                    GstVideoRegionOfInterestMeta *roi) {
    auto inference = gva_base_inference->inference;
    g_assert(inference != nullptr);

    if (!InferenceImpl::IsRoiSizeValid(roi))
        return false;
    // Check if object-class is the same as roi class label
    if (!inference->FilterObjectClass(roi))
        return false;

    if (gva_base_inference->specific_roi_filter)
        return gva_base_inference->specific_roi_filter(gva_base_inference, current_num_frame, buffer, roi);
    return true;
}

static GstCaps *gva_base_inference_transform_caps(GstBaseTransform *trans, GstPadDirection direction, GstCaps *caps,
                                                  GstCaps *filter) {
    GvaBaseInference *base_inference = GVA_BASE_INFERENCE(trans);

    // Get the default transformed caps from the parent class
    GstCaps *result =
        GST_BASE_TRANSFORM_CLASS(gva_base_inference_parent_class)->transform_caps(trans, direction, caps, filter);

    // If device is CPU, filter out memory:VASurface, memory:VAMemory, memory:DMABuf
    if (base_inference->device && g_strcmp0(base_inference->device, "CPU") == 0) {
        GstCaps *filtered = gst_caps_copy(result);
        for (gint i = gst_caps_get_size(filtered) - 1; i >= 0; --i) {
            GstCapsFeatures *features = gst_caps_get_features(filtered, i);
            if ((gst_caps_features_contains(features, "memory:VASurface")) ||
                (gst_caps_features_contains(features, "memory:VAMemory")) ||
                (gst_caps_features_contains(features, "memory:DMABuf")) ||
                (gst_caps_features_contains(features, "memory:D3D11Memory"))) {
                gst_caps_remove_structure(filtered, i);
                GST_WARNING("Filtered out structure %d from caps, it contains unsupported memory type", i);
            }
        }
        gst_caps_unref(result);
        result = filtered;
    }

    return result;
}

void gva_base_inference_class_init(GvaBaseInferenceClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gva_base_inference_debug_category, "gva_base_inference", 0,
                            "debug category for base inference element");

    gobject_class->set_property = gva_base_inference_set_property;
    gobject_class->get_property = gva_base_inference_get_property;
    gobject_class->dispose = gva_base_inference_dispose;
    gobject_class->finalize = gva_base_inference_finalize;
    base_transform_class->transform_caps = GST_DEBUG_FUNCPTR(gva_base_inference_transform_caps);
    base_transform_class->set_caps = GST_DEBUG_FUNCPTR(gva_base_inference_set_caps);
    base_transform_class->start = GST_DEBUG_FUNCPTR(gva_base_inference_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gva_base_inference_stop);
    base_transform_class->sink_event = GST_DEBUG_FUNCPTR(gva_base_inference_sink_event);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gva_base_inference_transform_ip);
    base_transform_class->propose_allocation = GST_DEBUG_FUNCPTR(gva_base_inference_propose_allocation);
    element_class->change_state = GST_DEBUG_FUNCPTR(gva_base_inference_change_state);

    klass->on_initialized = on_base_inference_initialized;

    constexpr auto param_flags = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_property(
        gobject_class, PROP_MODEL,
        g_param_spec_string("model", "Model", "Path to inference model network file", DEFAULT_MODEL, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_CUSTOM_PREPROC_LIB,
        g_param_spec_string("custom-preproc-lib", "Custom Pre-processing Library",
                            "Path to the .so file defining custom input image pre-processing",
                            DEFAULT_CUSTOM_PREPROC_LIB, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_CUSTOM_POSTPROC_LIB,
        g_param_spec_string("custom-postproc-lib", "Custom Post-processing Library",
                            "Path to the .so file defining custom model output converter. "
                            "The library must implement the Convert function: "
                            "void Convert(GstTensorMeta *outputTensors, const GstStructure *network, "
                            "const GstStructure *params, GstAnalyticsRelationMeta *relationMeta);",
                            DEFAULT_CUSTOM_POSTPROC_LIB, param_flags));

    g_object_class_install_property(gobject_class, PROP_OV_EXTENSION_LIB,
                                    g_param_spec_string("ov-extension-lib", "OpenVINO Extension Library",
                                                        "Path to the .so file defining custom OpenVINO operations.",
                                                        DEFAULT_OV_EXTENSION_LIB, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_MODEL_INSTANCE_ID,
        g_param_spec_string(
            "model-instance-id", "Model Instance Id",
            "Identifier for sharing a loaded model instance between elements of the same type. Elements with the "
            "same model-instance-id will share all model and inference engine related properties",
            DEFAULT_MODEL_INSTANCE_ID, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_SCHEDULING_POLICY,
        g_param_spec_string("scheduling-policy", "Scheduling Policy",
                            "Scheduling policy across streams sharing same model instance: "
                            "throughput (select first incoming frame), "
                            "latency (select frames with earliest presentation time out of the streams sharing same "
                            "model-instance-id; recommended batch-size less than or equal to the number of streams) ",
                            DEFAULT_SCHEDULING_POLICY, (GParamFlags)(param_flags)));

    g_object_class_install_property(
        gobject_class, PROP_PRE_PROC_BACKEND,
        g_param_spec_string(
            "pre-process-backend", "Pre-processing method",
            "Select a pre-processing method (color conversion, resize and crop), "
            "one of 'ie', 'opencv', 'va', 'va-surface-sharing, 'vaapi', 'vaapi-surface-sharing'."
            " If not set, it will be selected automatically: 'va' for VAMemory and DMABuf, 'ie' for SYSTEM memory.",
            DEFAULT_PRE_PROC, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_MODEL_PROC,
        g_param_spec_string("model-proc", "Model preproc and postproc",
                            "Path to JSON file with description of input/output layers pre-processing/post-processing",
                            DEFAULT_MODEL_PROC, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_DEVICE,
        g_param_spec_string(
            "device", "Device",
            "Target device for inference. Please see OpenVINO™ Toolkit documentation for list of supported devices.",
            DEFAULT_DEVICE, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_BATCH_SIZE,
        g_param_spec_uint("batch-size", "Batch size",
                          "Number of frames batched together for a single inference. If the batch-size is 0, then it "
                          "will be set by default to be optimal for the device."
                          "Not all models support batching. Use model optimizer to ensure "
                          "that the model has batching support.",
                          DEFAULT_MIN_BATCH_SIZE, DEFAULT_MAX_BATCH_SIZE, DEFAULT_BATCH_SIZE, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_INFERENCE_INTERVAL,
        g_param_spec_uint("inference-interval", "Inference Interval",
                          "Interval between inference requests. An interval of 1 (Default) performs inference on "
                          "every frame. An interval of 2 performs inference on every other frame. An interval of N "
                          "performs inference on every Nth frame.",
                          DEFAULT_MIN_INFERENCE_INTERVAL, DEFAULT_MAX_INFERENCE_INTERVAL, DEFAULT_INFERENCE_INTERVAL,
                          param_flags));

    g_object_class_install_property(
        gobject_class, PROP_RESHAPE,
        g_param_spec_boolean("reshape", "Reshape input layer",
                             "If true, model input layer will be reshaped to resolution of input frames "
                             "(no resize operation before inference). "
                             "Note: this feature has limitations, not all network supports reshaping.",
                             DEFAULT_RESHAPE, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_RESHAPE_WIDTH,
        g_param_spec_uint("reshape-width", "Width for reshape", "Width to which the network will be reshaped.",
                          DEFAULT_MIN_RESHAPE_WIDTH, DEFAULT_MAX_RESHAPE_WIDTH, DEFAULT_RESHAPE_WIDTH, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_RESHAPE_HEIGHT,
        g_param_spec_uint("reshape-height", "Height for reshape", "Height to which the network will be reshaped.",
                          DEFAULT_MIN_RESHAPE_HEIGHT, DEFAULT_MAX_RESHAPE_HEIGHT, DEFAULT_RESHAPE_HEIGHT, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_NO_BLOCK,
        g_param_spec_boolean(
            "no-block", "Adaptive inference skipping",
            "(Experimental) Option to help maintain frames per second of incoming stream. Skips inference "
            "on an incoming frame if all inference requests are currently processing outstanding frames",
            DEFAULT_NO_BLOCK, static_cast<GParamFlags>(param_flags | G_PARAM_DEPRECATED)));

    g_object_class_install_property(gobject_class, PROP_NIREQ,
                                    g_param_spec_uint("nireq", "NIReq", "Number of inference requests",
                                                      DEFAULT_MIN_NIREQ, DEFAULT_MAX_NIREQ, DEFAULT_NIREQ,
                                                      param_flags));

    g_object_class_install_property(
        gobject_class, PROP_CPU_THROUGHPUT_STREAMS,
        g_param_spec_uint("cpu-throughput-streams", "CPU-Throughput-Streams",
                          "Deprecated. Use ie-config=CPU_THROUGHPUT_STREAMS=<number-streams> instead",
                          DEFAULT_MIN_CPU_THROUGHPUT_STREAMS, DEFAULT_MAX_CPU_THROUGHPUT_STREAMS,
                          DEFAULT_CPU_THROUGHPUT_STREAMS, static_cast<GParamFlags>(param_flags | G_PARAM_DEPRECATED)));

    g_object_class_install_property(
        gobject_class, PROP_GPU_THROUGHPUT_STREAMS,
        g_param_spec_uint("gpu-throughput-streams", "GPU-Throughput-Streams",
                          "Deprecated. Use ie-config=GPU_THROUGHPUT_STREAMS=<number-streams> instead",
                          DEFAULT_MIN_GPU_THROUGHPUT_STREAMS, DEFAULT_MAX_GPU_THROUGHPUT_STREAMS,
                          DEFAULT_GPU_THROUGHPUT_STREAMS, static_cast<GParamFlags>(param_flags | G_PARAM_DEPRECATED)));

    g_object_class_install_property(
        gobject_class, PROP_IE_CONFIG,
        g_param_spec_string("ie-config", "Inference-Engine-Config",
                            "Comma separated list of KEY=VALUE parameters for Inference Engine configuration. "
                            "See OpenVINO™ Toolkit documentation for available parameters",
                            "", param_flags));

    g_object_class_install_property(
        gobject_class, PROP_PRE_PROC_CONFIG,
        g_param_spec_string("pre-process-config", "Pre-processing configuration",
                            "Comma separated list of KEY=VALUE parameters for image processing pipeline configuration",
                            "", param_flags));

    g_object_class_install_property(
        gobject_class, PROP_INFERENCE_REGION,
        g_param_spec_enum("inference-region", "Inference-Region",
                          "Identifier responsible for the region on which inference will be performed",
                          GST_TYPE_GVA_BASE_INFERENCE_REGION, DEFAULT_INFERENCE_REGION, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_OBJECT_CLASS,
        g_param_spec_string("object-class", "ObjectClass",
                            "Filter for Region of Interest class label on this element input", DEFAULT_OBJECT_CLASS,
                            (GParamFlags)(param_flags)));

    g_object_class_install_property(
        gobject_class, PROP_LABELS,
        g_param_spec_string("labels", "Labels",
                            "Array of object classes. "
                            "It could be set as the following example: labels=<label1,label2,label3>",
                            DEFAULT_LABELS, param_flags));

    g_object_class_install_property(gobject_class, PROP_LABELS_FILE,
                                    g_param_spec_string("labels-file", "Labels-file",
                                                        "Path to .txt file containing object classes (one per line)",
                                                        DEFAULT_LABELS, param_flags));

    g_object_class_install_property(gobject_class, PROP_SCALE_METHOD,
                                    g_param_spec_string("scale-method", "scale-method",
                                                        "Scale method to use in pre-preprocessing before inference."
                                                        " Only default and scale-method=fast (VAAPI based) supported "
                                                        "in this element",
                                                        nullptr, param_flags));

    g_object_class_install_property(
        gobject_class, PROP_SHARE_VADISPLAY_CTX,
        g_param_spec_boolean("share-va-display-ctx", "Share VA Display Context",
                             "Whether to share VA Display context across inference elements: "
                             "true (share context, default), false (do not share context)",
                             DEFAULT_SHARE_VADISPLAY_CTX, param_flags));
}

void gva_base_inference_cleanup(GvaBaseInference *base_inference) {
    if (base_inference == nullptr)
        return;

    GST_DEBUG_OBJECT(base_inference, "gva_base_inference_cleanup");

    if (base_inference->inference) {
        release_inference_instance(base_inference);
        base_inference->inference = nullptr;
    }

    if (base_inference->priv) {
        base_inference->priv->~GvaBaseInferencePrivate();
        base_inference->priv = nullptr;
    }

    g_free(base_inference->model);
    base_inference->model = nullptr;

    g_free(base_inference->device);
    base_inference->device = nullptr;

    g_free(base_inference->model_proc);
    base_inference->model_proc = nullptr;

    g_free(base_inference->model_instance_id);
    base_inference->model_instance_id = nullptr;

    g_free(base_inference->scheduling_policy);
    base_inference->scheduling_policy = nullptr;

    g_free(base_inference->pre_proc_type);
    base_inference->pre_proc_type = nullptr;

    g_free(base_inference->ie_config);
    base_inference->ie_config = nullptr;

    g_free(base_inference->pre_proc_config);
    base_inference->pre_proc_config = nullptr;

    g_free(base_inference->allocator_name);
    base_inference->allocator_name = nullptr;

    if (base_inference->info) {
        gst_video_info_free(base_inference->info);
        base_inference->info = nullptr;
    }

    base_inference->initialized = FALSE;

    base_inference->num_skipped_frames = UINT_MAX - 1; // always run inference on first frame
    base_inference->frame_num = DEFAULT_FIRST_FRAME_NUM;

    g_free(base_inference->object_class);
    base_inference->object_class = nullptr;

    g_free(base_inference->labels);
    base_inference->labels = nullptr;

    releasePostProcessor(base_inference->post_proc);
    base_inference->post_proc = nullptr;

    g_free(base_inference->scale_method);
    base_inference->scale_method = nullptr;

    g_free(base_inference->custom_preproc_lib);
    base_inference->custom_preproc_lib = nullptr;

    g_free(base_inference->custom_postproc_lib);
    base_inference->custom_postproc_lib = nullptr;

    g_free(base_inference->ov_extension_lib);
    base_inference->ov_extension_lib = nullptr;
}

void gva_base_inference_init(GvaBaseInference *base_inference) {
    GST_DEBUG_OBJECT(base_inference, "gva_base_inference_init");

    if (base_inference == nullptr)
        return;

    gva_base_inference_cleanup(base_inference);

    // Intialization of private data
    auto *priv_memory = gva_base_inference_get_instance_private(base_inference);
    // This won't be converted to shared ptr because of memory placement
    base_inference->priv = new (priv_memory) GvaBaseInferencePrivate();

    base_inference->model = g_strdup(DEFAULT_MODEL);
    base_inference->device = g_strdup(DEFAULT_DEVICE);
    base_inference->model_proc = g_strdup(DEFAULT_MODEL_PROC);
    base_inference->inference_interval = DEFAULT_INFERENCE_INTERVAL;
    base_inference->reshape = DEFAULT_RESHAPE;
    base_inference->batch_size = DEFAULT_BATCH_SIZE;
    base_inference->reshape_width = DEFAULT_RESHAPE_WIDTH;
    base_inference->reshape_height = DEFAULT_RESHAPE_HEIGHT;
    base_inference->no_block = DEFAULT_NO_BLOCK;
    base_inference->nireq = DEFAULT_NIREQ;
    base_inference->model_instance_id = g_strdup(DEFAULT_MODEL_INSTANCE_ID);
    base_inference->scheduling_policy = g_strdup(DEFAULT_SCHEDULING_POLICY);
    base_inference->pre_proc_type = g_strdup(DEFAULT_PRE_PROC);
    // TODO: make one property for streams
    base_inference->cpu_streams = DEFAULT_CPU_THROUGHPUT_STREAMS;
    base_inference->gpu_streams = DEFAULT_GPU_THROUGHPUT_STREAMS;
    base_inference->ie_config = g_strdup("");
    base_inference->pre_proc_config = g_strdup("");
    base_inference->allocator_name = g_strdup(DEFAULT_ALLOCATOR_NAME);

    base_inference->initialized = FALSE;
    base_inference->info = nullptr;
    base_inference->inference_region = DEFAULT_INFERENCE_REGION;
    base_inference->inference = nullptr;

    base_inference->is_roi_inference_needed = &is_roi_inference_needed;
    base_inference->specific_roi_filter = nullptr;

    base_inference->pre_proc = nullptr;
    base_inference->input_prerocessors_factory = GET_INPUT_PREPROCESSORS;
    base_inference->post_proc = nullptr;

    base_inference->frame_num = DEFAULT_FIRST_FRAME_NUM;
    base_inference->object_class = DEFAULT_OBJECT_CLASS;
    base_inference->labels = DEFAULT_LABELS;
    base_inference->scale_method = nullptr;
    base_inference->custom_preproc_lib = g_strdup(DEFAULT_MODEL_PROC);
    base_inference->custom_postproc_lib = g_strdup(DEFAULT_CUSTOM_POSTPROC_LIB);
    base_inference->ov_extension_lib = g_strdup(DEFAULT_OV_EXTENSION_LIB);

    base_inference->share_va_display_ctx = DEFAULT_SHARE_VADISPLAY_CTX;
}

GstStateChangeReturn gva_base_inference_change_state(GstElement *element, GstStateChange transition) {
    GvaBaseInference *base_inference = GVA_BASE_INFERENCE(element);
    GST_DEBUG_OBJECT(base_inference, "gva_base_inference_change_state");

    if (base_inference->inference && (transition == GST_STATE_CHANGE_PAUSED_TO_PLAYING)) {
        GST_DEBUG_OBJECT(base_inference, "Flushing outputs on transition to PLAYING state");
        base_inference->inference->FlushOutputs();
    }

    return GST_ELEMENT_CLASS(gva_base_inference_parent_class)->change_state(element, transition);
}

gboolean check_gva_base_inference_stopped(GvaBaseInference *base_inference) {
    GstState state;
    gboolean is_stopped;

    GST_OBJECT_LOCK(base_inference);
    state = GST_STATE(base_inference);
    is_stopped = state == GST_STATE_READY || state == GST_STATE_NULL;
    GST_OBJECT_UNLOCK(base_inference);
    return is_stopped;
}

gboolean gva_base_inference_propose_allocation(GstBaseTransform *trans, GstQuery *decide_query, GstQuery *query) {
    UNUSED(decide_query);
    UNUSED(trans);
    if (query) {
        gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, nullptr);
        return TRUE;
    }
    return FALSE;
}

void gva_base_inference_set_model(GvaBaseInference *base_inference, const gchar *model_path) {
    if (check_gva_base_inference_stopped(base_inference)) {
        g_free(base_inference->model);
        base_inference->model = g_strdup(model_path);
        GST_INFO_OBJECT(base_inference, "model: %s", base_inference->model);
    } else {
        GST_ELEMENT_ERROR(base_inference, RESOURCE, SETTINGS, ("'model' can't be changed"),
                          ("You cannot change 'model' property on base_inference when a file is open"));
    }
}

void gva_base_inference_set_model_proc(GvaBaseInference *base_inference, const gchar *model_proc_path) {
    if (check_gva_base_inference_stopped(base_inference)) {
        g_free(base_inference->model_proc);
        base_inference->model_proc = g_strdup(model_proc_path);
        GST_INFO_OBJECT(base_inference, "model-proc: %s", base_inference->model_proc);
    } else {
        GST_ELEMENT_WARNING(base_inference, RESOURCE, SETTINGS, ("'model-proc' can't be changed"),
                            ("You cannot change 'model-proc' property on base_inference when a file is open"));
    }
}

void gva_base_inference_set_labels(GvaBaseInference *base_inference, const gchar *labels_str) {
    if (check_gva_base_inference_stopped(base_inference)) {
        if (base_inference->labels)
            g_free(base_inference->labels);

        base_inference->labels = g_strdup(labels_str);
        GST_INFO_OBJECT(base_inference, "labels: %s", base_inference->labels);
    } else {
        GST_ELEMENT_WARNING(base_inference, RESOURCE, SETTINGS, ("'labels' can't be changed"),
                            ("You cannot change 'labels' property on base_inference when a file is open"));
    }
}

void gva_base_inference_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    GvaBaseInference *base_inference = GVA_BASE_INFERENCE(object);

    GST_DEBUG_OBJECT(base_inference, "set_property");

    switch (property_id) {
    case PROP_MODEL:
        gva_base_inference_set_model(base_inference, g_value_get_string(value));
        break;
    case PROP_MODEL_PROC:
        gva_base_inference_set_model_proc(base_inference, g_value_get_string(value));
        break;
    case PROP_DEVICE:
        g_free(base_inference->device);
        base_inference->device = g_value_dup_string(value);
        break;
    case PROP_INFERENCE_INTERVAL:
        base_inference->inference_interval = g_value_get_uint(value);
        break;
    case PROP_RESHAPE:
        base_inference->reshape = g_value_get_boolean(value);
        break;
    case PROP_BATCH_SIZE:
        base_inference->batch_size = g_value_get_uint(value);
        break;
    case PROP_RESHAPE_WIDTH:
        base_inference->reshape_width = g_value_get_uint(value);
        break;
    case PROP_RESHAPE_HEIGHT:
        base_inference->reshape_height = g_value_get_uint(value);
        break;
    case PROP_NO_BLOCK:
        base_inference->no_block = g_value_get_boolean(value);
        break;
    case PROP_NIREQ:
        base_inference->nireq = g_value_get_uint(value);
        break;
    case PROP_MODEL_INSTANCE_ID:
        g_free(base_inference->model_instance_id);
        base_inference->model_instance_id = g_value_dup_string(value);
        break;
    case PROP_SCHEDULING_POLICY:
        g_free(base_inference->scheduling_policy);
        base_inference->scheduling_policy = g_value_dup_string(value);
        break;
    case PROP_PRE_PROC_BACKEND:
        g_free(base_inference->pre_proc_type);
        base_inference->pre_proc_type = g_value_dup_string(value);
        if (strcmp(base_inference->pre_proc_type, "vaapi") == 0)
            GST_WARNING("The property pre-process-backend=vaapi is deprecated and will be removed in "
                        "future versions, please use pre-process-backend=va instead.");
        if (strcmp(base_inference->pre_proc_type, "vaapi-surface-sharing") == 0)
            GST_WARNING("The property pre-process-backend=vaapi-surface-sharing is deprecated and will be removed "
                        "in future versions, please use pre-process-backend=va-surface-sharing instead.");
        break;
    case PROP_CPU_THROUGHPUT_STREAMS:
        GST_WARNING("The property <cpu-throughput-streams> is deprecated and will be removed in future versions, "
                    "please use ie-config=NUM_STREAMS=x instead.");
        base_inference->cpu_streams = g_value_get_uint(value);
        break;
    case PROP_GPU_THROUGHPUT_STREAMS:
        GST_WARNING("The property <gpu-throughput-streams> is deprecated and will be removed in future versions, "
                    "please use ie-config=NUM_STREAMS=x instead.");
        base_inference->gpu_streams = g_value_get_uint(value);
        break;
    case PROP_IE_CONFIG:
        g_free(base_inference->ie_config);
        base_inference->ie_config = g_value_dup_string(value);
        break;
    case PROP_PRE_PROC_CONFIG:
        g_free(base_inference->pre_proc_config);
        base_inference->pre_proc_config = g_value_dup_string(value);
        break;
    case PROP_INFERENCE_REGION:
        base_inference->inference_region = static_cast<InferenceRegionType>(g_value_get_enum(value));
        break;
    case PROP_OBJECT_CLASS:
        g_free(base_inference->object_class);
        base_inference->object_class = g_value_dup_string(value);
        // It is necessary to update the vector of object classes after possible change of this property
        gva_base_inference_update_object_classes(base_inference);
        break;
    case PROP_LABELS: {
        auto str = g_value_get_string(value);
        if (str && str[0] == '<') { // if specified as <label1,label2,label3>
            GValue garr = {};
            gst_value_deserialize(&garr, str);
            std::vector<std::string> vec(gst_value_array_get_size(&garr));
            for (size_t i = 0; i < vec.size(); i++)
                vec[i] = g_value_get_string((gst_value_array_get_value(&garr, i)));
            auto vec_str = Utils::join(vec.begin(), vec.end());
            gva_base_inference_set_labels(base_inference, vec_str.data());
            g_value_unset(&garr);
        } else {
            gva_base_inference_set_labels(base_inference, str);
        }
    } break;
    case PROP_LABELS_FILE:
        gva_base_inference_set_labels(base_inference, g_value_get_string(value));
        break;
    case PROP_SCALE_METHOD:
        if (std::string(g_value_get_string(value)) == "fast") {
            g_free(base_inference->scale_method);
            base_inference->scale_method = g_value_dup_string(value);
            base_inference->pre_proc_config = g_strdup("VAAPI_FAST_SCALE_LOAD_FACTOR=1");
        } else
            GST_ERROR_OBJECT(base_inference, "Unsupported scale-method=%s", g_value_get_string(value));
        break;
    case PROP_CUSTOM_PREPROC_LIB:
        g_free(base_inference->custom_preproc_lib);
        base_inference->custom_preproc_lib = g_value_dup_string(value);
        GST_INFO_OBJECT(base_inference, "custom-preproc-lib: %s", base_inference->custom_preproc_lib);
        break;
    case PROP_CUSTOM_POSTPROC_LIB:
        g_free(base_inference->custom_postproc_lib);
        base_inference->custom_postproc_lib = g_value_dup_string(value);
        GST_INFO_OBJECT(base_inference, "custom-postproc-lib: %s", base_inference->custom_postproc_lib);
        break;
    case PROP_OV_EXTENSION_LIB:
        g_free(base_inference->ov_extension_lib);
        base_inference->ov_extension_lib = g_value_dup_string(value);
        GST_INFO_OBJECT(base_inference, "ov-extension-lib: %s", base_inference->ov_extension_lib);
        break;
    case PROP_SHARE_VADISPLAY_CTX:
        base_inference->share_va_display_ctx = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gva_base_inference_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GvaBaseInference *base_inference = GVA_BASE_INFERENCE(object);

    GST_DEBUG_OBJECT(base_inference, "get_property");

    switch (property_id) {
    case PROP_MODEL:
        g_value_set_string(value, base_inference->model);
        break;
    case PROP_MODEL_PROC:
        g_value_set_string(value, base_inference->model_proc);
        break;
    case PROP_DEVICE:
        g_value_set_string(value, base_inference->device);
        break;
    case PROP_INFERENCE_INTERVAL:
        g_value_set_uint(value, base_inference->inference_interval);
        break;
    case PROP_RESHAPE:
        g_value_set_boolean(value, base_inference->reshape);
        break;
    case PROP_BATCH_SIZE:
        g_value_set_uint(value, base_inference->batch_size);
        break;
    case PROP_RESHAPE_WIDTH:
        g_value_set_uint(value, base_inference->reshape_width);
        break;
    case PROP_RESHAPE_HEIGHT:
        g_value_set_uint(value, base_inference->reshape_height);
        break;
    case PROP_NO_BLOCK:
        g_value_set_boolean(value, base_inference->no_block);
        break;
    case PROP_NIREQ:
        g_value_set_uint(value, base_inference->nireq);
        break;
    case PROP_MODEL_INSTANCE_ID:
        g_value_set_string(value, base_inference->model_instance_id);
        break;
    case PROP_PRE_PROC_BACKEND:
        g_value_set_string(value, base_inference->pre_proc_type);
        break;
    case PROP_CPU_THROUGHPUT_STREAMS:
        g_value_set_uint(value, base_inference->cpu_streams);
        break;
    case PROP_GPU_THROUGHPUT_STREAMS:
        g_value_set_uint(value, base_inference->gpu_streams);
        break;
    case PROP_IE_CONFIG:
        g_value_set_string(value, base_inference->ie_config);
        break;
    case PROP_PRE_PROC_CONFIG:
        g_value_set_string(value, base_inference->pre_proc_config);
        break;
    case PROP_INFERENCE_REGION:
        g_value_set_enum(value, base_inference->inference_region);
        break;
    case PROP_OBJECT_CLASS:
        g_value_set_string(value, base_inference->object_class);
        break;
    case PROP_LABELS:
        g_value_set_string(value, base_inference->labels);
        break;
    case PROP_LABELS_FILE:
        g_value_set_string(value, base_inference->labels);
        break;
    case PROP_SCALE_METHOD:
        g_value_set_string(value, base_inference->scale_method);
        break;
    case PROP_CUSTOM_PREPROC_LIB:
        g_value_set_string(value, base_inference->custom_preproc_lib);
        break;
    case PROP_CUSTOM_POSTPROC_LIB:
        g_value_set_string(value, base_inference->custom_postproc_lib);
        break;
    case PROP_OV_EXTENSION_LIB:
        g_value_set_string(value, base_inference->ov_extension_lib);
        break;
    case PROP_SHARE_VADISPLAY_CTX:
        g_value_set_boolean(value, base_inference->share_va_display_ctx);
        break;
    case PROP_SCHEDULING_POLICY:
        g_value_set_string(value, base_inference->scheduling_policy);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gva_base_inference_dispose(GObject *object) {
    GvaBaseInference *base_inference = GVA_BASE_INFERENCE(object);

    GST_DEBUG_OBJECT(base_inference, "dispose");

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS(gva_base_inference_parent_class)->dispose(object);
}

void gva_base_inference_finalize(GObject *object) {
    GvaBaseInference *base_inference = GVA_BASE_INFERENCE(object);

    GST_DEBUG_OBJECT(base_inference, "finalize");

    /* clean up object here */
    gva_base_inference_cleanup(base_inference);

    G_OBJECT_CLASS(gva_base_inference_parent_class)->finalize(object);
}

gboolean gva_base_inference_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    UNUSED(outcaps);

    GvaBaseInference *base_inference = GVA_BASE_INFERENCE(trans);

    GST_DEBUG_OBJECT(base_inference, "set_caps");

    GstVideoInfo video_info;
    gst_video_info_from_caps(&video_info, incaps);
    CapsFeature caps_feature = get_caps_feature(incaps);

    // if DMA_DRM format detected, convert to 'traditional' video format
    if (gst_video_is_dma_drm_caps(incaps)) {
        GstVideoInfoDmaDrm dma_info;

        if (!gst_video_info_dma_drm_from_caps(&dma_info, incaps))
            return FALSE;

        if (!gst_video_info_dma_drm_to_video_info(&dma_info, &video_info))
            return FALSE;
    }

    // Check if the caps are compatible with the device
    if ((base_inference->device && g_strcmp0(base_inference->device, "CPU") == 0 &&
         ((gst_caps_features_contains(gst_caps_get_features(incaps, 0), "memory:VASurface")) ||
          (gst_caps_features_contains(gst_caps_get_features(incaps, 0), "memory:VAMemory")) ||
          (gst_caps_features_contains(gst_caps_get_features(incaps, 0), "memory:DMABuf")) ||
          (gst_caps_features_contains(gst_caps_get_features(incaps, 0), "memory:D3D11Memory"))))) {
        GST_ELEMENT_WARNING(base_inference, RESOURCE, SETTINGS,
                            ("Refusing caps other than SYSTEM_MEMORY_CAPS because device is set to CPU"),
                            ("Set device property to a hardware accelerator (e.g., GPU) to enable VA memory types."));
        return FALSE;
    }

    auto element_name = GST_ELEMENT_NAME(GST_ELEMENT(base_inference));
    // convert element_name to std::string and remove trailing numbers from element name
    std::string element_name_str = std::string(element_name);
    auto pos = element_name_str.find_last_of("0123456789");
    if (pos != std::string::npos)
        element_name_str = element_name_str.substr(0, pos);

    if (base_inference->device && g_strcmp0(base_inference->device, "CPU") != 0 &&
        (gst_caps_features_contains(gst_caps_get_features(incaps, 0), "memory:SystemMemory"))) {
        GST_ELEMENT_WARNING(
            base_inference, RESOURCE, SETTINGS,
            ("\n\nSystem memory is being used for inference on device '%s'. For optimal performance, use "
             "VA memory in the pipeline:\n\nvapostproc ! \"video/x-raw(memory:VAMemory)\" ! %s device=%s model=%s.\n",
             base_inference->device, element_name_str.c_str(), base_inference->device, base_inference->model),
            ("System memory transfers are less efficient than VA memory for device '%s'. Consider "
             "using memory:VAMemory for better performance. \n",
             base_inference->device));
    }

    if (base_inference->inference && base_inference->info &&
        gst_video_info_is_equal(base_inference->info, &video_info) && base_inference->caps_feature == caps_feature) {
        // We alredy have an inference model instance.
        return TRUE;
    }

    if (base_inference->inference) {
        base_inference->inference->FlushInference();
        release_inference_instance(base_inference);
        base_inference->inference = nullptr;
    }

    if (base_inference->info) {
        gst_video_info_free(base_inference->info);
        base_inference->info = NULL;
    }
    base_inference->info = gst_video_info_copy(&video_info);
    base_inference->caps_feature = caps_feature;

    base_inference->priv->buffer_mapper.reset();

    // Need to acquire inference model instance
    try {
        if (!base_inference->priv->va_display && (base_inference->caps_feature == VA_SURFACE_CAPS_FEATURE ||
                                                  base_inference->caps_feature == VA_MEMORY_CAPS_FEATURE ||
                                                  base_inference->caps_feature == DMA_BUF_CAPS_FEATURE)) {

            // Try to query VADisplay from decoder. Select dlstreamer::MemoryType::VA memory type as default.
            try {
                base_inference->priv->va_display = std::make_shared<dlstreamer::GSTContextQuery>(
                    trans, (base_inference->caps_feature == VA_SURFACE_CAPS_FEATURE) ? dlstreamer::MemoryType::VAAPI
                                                                                     : dlstreamer::MemoryType::VA);
                GST_INFO_OBJECT(trans, "Got VADisplay (%p) from query", base_inference->priv->va_display.get());
            } catch (...) {
                GST_WARNING_OBJECT(trans, "Couldn't query VADisplay from gstreamer-vaapi elements. Possible reason: "
                                          "gstreamer-vaapi isn't built with required patches");
            }
        }
#ifdef _WIN32
        if (!base_inference->priv->d3d11_device && (base_inference->caps_feature == D3D11_MEMORY_CAPS_FEATURE)) {
            // Try to query D3D11Device from decoder. Select dlstreamer::MemoryType::D3D11 memory type as default.
            try {
                base_inference->priv->d3d11_device =
                    std::make_shared<dlstreamer::GSTContextQuery>(trans, dlstreamer::MemoryType::D3D11);
                GST_INFO_OBJECT(trans, "Got D3D11Device (%p) from query", base_inference->priv->d3d11_device.get());
            } catch (...) {
                GST_WARNING_OBJECT(trans, "Couldn't query D3D11Device from gstreamer-d3d11 elements.");
            }
        }
#endif

        base_inference->inference = acquire_inference_instance(base_inference).get();
        if (!base_inference->inference)
            throw std::runtime_error("inference is NULL.");

        GvaBaseInferenceClass *base_inference_class = GVA_BASE_INFERENCE_GET_CLASS(base_inference);
        if (base_inference_class->on_initialized) {
            base_inference_class->on_initialized(base_inference);

            if (!base_inference->post_proc)
                throw std::runtime_error("post-processing is NULL.");
        }

        // Create a buffer mapper once we know the target memory type
#ifdef _WIN32
        if (base_inference->caps_feature == D3D11_MEMORY_CAPS_FEATURE) {
            base_inference->priv->buffer_mapper =
                BufferMapperFactory::createMapper(base_inference->inference->GetInferenceMemoryType(),
                                                  base_inference->info, base_inference->priv->d3d11_device);
        } else {
            base_inference->priv->buffer_mapper =
                BufferMapperFactory::createMapper(base_inference->inference->GetInferenceMemoryType(),
                                                  base_inference->info, base_inference->priv->va_display);
        }
#else
        base_inference->priv->buffer_mapper =
            BufferMapperFactory::createMapper(base_inference->inference->GetInferenceMemoryType(), base_inference->info,
                                              base_inference->priv->va_display);
#endif

        if (!base_inference->priv->buffer_mapper)
            throw std::runtime_error("couldn't create buffer mapper");

        // We need to set the vector of object classes after InferenceImpl instance acquirement
        gva_base_inference_update_object_classes(base_inference);
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(base_inference, LIBRARY, INIT,
                          ("base_inference based element initialization has been failed."),
                          ("%s", Utils::createNestedErrorMsg(e).c_str()));
        return FALSE;
    }

    return TRUE;
}

void gva_base_inference_update_object_classes(GvaBaseInference *base_inference) {
    g_assert(base_inference);
    if (!base_inference->inference) {
        GST_ELEMENT_INFO(base_inference, CORE, STATE_CHANGE, ("object classes update was not completed"),
                         ("empty inference instance: retry will be performed once instance will be acquired"));
        return;
    }

    try {
        base_inference->inference->UpdateObjectClasses(base_inference->object_class);
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(base_inference, CORE, STATE_CHANGE, ("base_inference failed on object classes updating"),
                          ("%s", Utils::createNestedErrorMsg(e).c_str()));
    }
}

gboolean gva_base_inference_check_properties_correctness(GvaBaseInference *base_inference) {
    if (!base_inference->model_instance_id) {
        base_inference->model_instance_id = g_strdup(GST_ELEMENT_NAME(GST_ELEMENT(base_inference)));

        if (base_inference->model == nullptr) {
            GST_ELEMENT_ERROR(base_inference, RESOURCE, NOT_FOUND, ("'model' is not set"),
                              ("'model' property is not set"));
            return FALSE;
        } else if (!g_file_test(base_inference->model, G_FILE_TEST_EXISTS)) {
            GST_ELEMENT_ERROR(base_inference, RESOURCE, NOT_FOUND, ("'model' does not exist"),
                              ("path %s set in 'model' does not exist", base_inference->model));
            return FALSE;
        }
    }

    if (base_inference->model_proc != nullptr && !g_file_test(base_inference->model_proc, G_FILE_TEST_EXISTS)) {
        GST_ELEMENT_ERROR(base_inference, RESOURCE, NOT_FOUND, ("'model-proc' does not exist"),
                          ("path %s set in 'model-proc' does not exist", base_inference->model_proc));
        return FALSE;
    }

    if (base_inference->inference_region == FULL_FRAME && base_inference->object_class &&
        !g_str_equal(base_inference->object_class, "")) {
        GST_ERROR_OBJECT(base_inference, ("You cannot use 'object-class' property if you set 'full-frame' for "
                                          "'inference-region' property."));
        return FALSE;
    }

    return TRUE;
}

static void on_base_inference_initialized(GvaBaseInference *base_inference) {
    GST_DEBUG_OBJECT(base_inference, "on_base_inference_initialized");

    base_inference->post_proc = createPostProcessor(base_inference->inference, base_inference);
}

gboolean gva_base_inference_start(GstBaseTransform *trans) {
    GvaBaseInference *base_inference = GVA_BASE_INFERENCE(trans);

    GST_DEBUG_OBJECT(base_inference, "start");

    GST_INFO_OBJECT(base_inference,
                    "%s inference parameters:\n -- Model: %s\n -- Model proc: %s\n "
                    "-- Device: %s\n -- Inference interval: %d\n -- Reshape: %s\n -- Batch size: %d\n "
                    "-- Reshape width: %d\n -- Reshape height: %d\n -- No block: %s\n -- Num of requests: %d\n "
                    "-- Model instance ID: %s\n -- CPU streams: %d\n -- GPU streams: %d\n -- IE config: %s\n "
                    "-- Allocator name: %s\n -- Preprocessing type: %s\n -- Object class: %s\n "
                    "-- Labels: %s\n",
                    GST_ELEMENT_NAME(GST_ELEMENT_CAST(base_inference)), base_inference->model,
                    base_inference->model_proc, base_inference->device, base_inference->inference_interval,
                    base_inference->reshape ? "true" : "false", base_inference->batch_size,
                    base_inference->reshape_width, base_inference->reshape_height,
                    base_inference->no_block ? "true" : "false", base_inference->nireq,
                    base_inference->model_instance_id, base_inference->cpu_streams, base_inference->gpu_streams,
                    base_inference->ie_config, base_inference->allocator_name, base_inference->pre_proc_type,
                    base_inference->object_class, base_inference->labels);

    if (!gva_base_inference_check_properties_correctness(base_inference)) {
        return base_inference->initialized;
    }

    gboolean success = registerElement(base_inference);
    if (!success)
        return base_inference->initialized;

    base_inference->initialized = TRUE;

    return base_inference->initialized;
}

gboolean gva_base_inference_stop(GstBaseTransform *trans) {
    GvaBaseInference *self = GVA_BASE_INFERENCE(trans);
    GST_DEBUG_OBJECT(self, "stop");

    if (!self || !self->inference) {
        GST_ELEMENT_ERROR(self, CORE, STATE_CHANGE, ("base_inference failed on stop"), ("empty inference instance"));
        return TRUE;
    }

    try {
        self->inference->FlushInference();
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(self, CORE, STATE_CHANGE, ("base_inference failed on stop"),
                          ("%s", Utils::createNestedErrorMsg(e).c_str()));
    }

    return TRUE;
}

gboolean gva_base_inference_sink_event(GstBaseTransform *trans, GstEvent *event) {
    GvaBaseInference *base_inference = GVA_BASE_INFERENCE(trans);

    GST_DEBUG_OBJECT(base_inference, "sink_event");

    try {
        if (base_inference->inference && (event->type == GST_EVENT_EOS || event->type == GST_EVENT_FLUSH_STOP)) {
            base_inference->inference->FlushInference();
        }
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(base_inference, CORE, EVENT, ("base_inference failed while handling sink"),
                          ("%s", Utils::createNestedErrorMsg(e).c_str()));
    }

    return GST_BASE_TRANSFORM_CLASS(gva_base_inference_parent_class)->sink_event(trans, event);
}

GstFlowReturn gva_base_inference_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GvaBaseInference *base_inference = GVA_BASE_INFERENCE(trans);

    GST_DEBUG_OBJECT(base_inference, "transform_ip");

    if (!base_inference->inference) { // TODO find a way to move this check out to initialization stage
        GST_ELEMENT_ERROR(base_inference, RESOURCE, SETTINGS,
                          ("There is no master element provided for base_inference elements with inference-id '%s'. At "
                           "least one element for each inference-id should have model path specified",
                           base_inference->model_instance_id),
                          (nullptr));
        return GST_FLOW_ERROR;
    }

    GstFlowReturn status;
    try {
        status = base_inference->inference->TransformFrameIp(base_inference, buf);
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(base_inference, STREAM, FAILED, ("base_inference failed on frame processing"),
                          ("%s", Utils::createNestedErrorMsg(e).c_str()));
        status = GST_FLOW_ERROR;
    }

    return status;
    /* return GST_FLOW_OK; FIXME shouldn't signal about dropping frames in inplace transform function*/
}
