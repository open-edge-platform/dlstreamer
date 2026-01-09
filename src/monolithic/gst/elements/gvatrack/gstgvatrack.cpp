/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvatrack.h"
#include "tracker_factory.h"
#include "utils.h"
#include "video_frame.h"

#include "inference_backend/buffer_mapper.h"
#include "inference_backend/logger.h"
#include "logger_functions.h"
#include <dlstreamer/gst/context.h>
#include <dlstreamer/memory_mapper_factory.h>

#if !defined(_WIN32) && defined(ENABLE_VAAPI)
#include <dlstreamer/vaapi/context.h>
#endif

#define ELEMENT_LONG_NAME "Object tracker (generates GstGvaObjectTrackerMeta, GstVideoRegionOfInterestMeta)"
#define ELEMENT_DESCRIPTION                                                                                            \
    "Performs object tracking using zero-term, zero-term-imageless, short-term-imageless, or Deep SORT tracking "      \
    "algorithms. Zero-term tracking assigns unique object IDs and requires object detection to run on every frame. "   \
    "Short-term tracking allows to track objects between frames, thereby reducing the need to run object detection "   \
    "on each frame. Imageless tracking forms object associations based on the movement and shape of objects, and it "  \
    "does not use image data. Deep SORT combines motion prediction (Kalman filter) with appearance features for "      \
    "robust tracking in complex scenarios. It works with pre-extracted features from gvainference."

GST_DEBUG_CATEGORY_STATIC(gst_gva_track_debug_category);
#define GST_CAT_DEFAULT gst_gva_track_debug_category

#define DEVICE_CPU "CPU"
#define DEVICE_GPU "GPU"

enum {
    PROP_0,
    PROP_DEVICE,
    PROP_TRACKING_TYPE,
    PROP_TRACKING_CONFIG,
    PROP_FEATURE_MODEL,
    PROP_DEEPSORT_TRCK_CFG,
};

G_DEFINE_TYPE_WITH_CODE(GstGvaTrack, gst_gva_track, GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gst_gva_track_debug_category, "gvatrack", 0,
                                                "debug category for gvatrack element"));

static void gst_gva_track_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gva_track_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_gva_track_dispose(GObject *object);
static void gst_gva_track_finalize(GObject *object);

static void gst_gva_track_cleanup(GstGvaTrack *gva_track);
static gboolean gst_gva_track_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);
static gboolean gst_gva_track_sink_event(GstBaseTransform *trans, GstEvent *event);
static gboolean gst_gva_track_start(GstBaseTransform *trans);
static gboolean gst_gva_track_stop(GstBaseTransform *trans);
static GstFlowReturn gst_gva_track_transform_ip(GstBaseTransform *trans, GstBuffer *buf);

static GstStateChangeReturn gst_gva_track_change_state(GstElement *element, GstStateChange transition);

static dlstreamer::MemoryMapperPtr create_mapper(GstGvaTrack *gva_track, dlstreamer::ContextPtr context) {
    const bool gpu_device = strncmp(gva_track->device, DEVICE_GPU, strlen(DEVICE_GPU)) == 0;
    if (!gpu_device)
        return BufferMapperFactory::createMapper(InferenceBackend::MemoryType::SYSTEM);

#if !defined(_WIN32) && defined(ENABLE_VAAPI)
    // GPU tracker expects VASurface or VAMemory as input.
    if (gva_track->caps_feature == VA_SURFACE_CAPS_FEATURE || gva_track->caps_feature == VA_MEMORY_CAPS_FEATURE)
        return BufferMapperFactory::createMapper(InferenceBackend::MemoryType::VAAPI, context);

    // In case of DMA memory create additional chain of mappers GST -> DMA -> VAAPI
    assert(gva_track->caps_feature == DMA_BUF_CAPS_FEATURE);
    auto dma = BufferMapperFactory::createMapper(InferenceBackend::MemoryType::DMA_BUFFER);
    auto dma_to_vaapi = std::make_shared<dlstreamer::MemoryMapperDMAToVAAPI>(nullptr, context);
    return std::make_shared<dlstreamer::MemoryMapperChain>(
        dlstreamer::MemoryMapperChain{std::move(dma), std::move(dma_to_vaapi)});
#else
    UNUSED(context);
    throw std::runtime_error("GPU not supported");
#endif
}

static bool init_tracker_obj(GstGvaTrack *gva_track) {
    g_assert(!gva_track->tracker);
    try {
        dlstreamer::ContextPtr gst_vaapi_ctx;
        if (gva_track->caps_feature == VA_SURFACE_CAPS_FEATURE || gva_track->caps_feature == VA_MEMORY_CAPS_FEATURE ||
            gva_track->caps_feature == DMA_BUF_CAPS_FEATURE)
            gst_vaapi_ctx = std::make_shared<dlstreamer::GSTContextQuery>(
                GST_BASE_TRANSFORM(gva_track), (gva_track->caps_feature == VA_MEMORY_CAPS_FEATURE)
                                                   ? dlstreamer::MemoryType::VA
                                                   : dlstreamer::MemoryType::VAAPI);

        auto mapper = create_mapper(gva_track, gst_vaapi_ctx);
        gva_track->tracker = TrackerFactory::Create(gva_track, mapper, gst_vaapi_ctx);
        if (!gva_track->tracker)
            throw std::runtime_error("Failed to create tracker of " + std::to_string(gva_track->tracking_type) +
                                     " tracking type");
        GST_INFO_OBJECT(gva_track, "initialized %s tracker instance", gva_track->device);
    } catch (const std::exception &e) {
        GST_ERROR_OBJECT(gva_track, "Can't initialize tracker on %s device: %s", gva_track->device,
                         Utils::createNestedErrorMsg(e).c_str());
        return false;
    }

    return true;
}

static void release_tracker_obj(GstGvaTrack *gva_track) {
    if (!gva_track->tracker)
        return;
    try {
        delete gva_track->tracker;
    } catch (const std::exception &e) {
        GST_ERROR_OBJECT(gva_track, "%s", e.what());
    }
    gva_track->tracker = nullptr;
}

void gst_gva_track_cleanup(GstGvaTrack *gva_track) {
    GST_DEBUG_OBJECT(gva_track, "gst_gva_track_cleanup");

    if (gva_track == NULL)
        return;

    release_tracker_obj(gva_track);

    g_free(gva_track->device);
    gva_track->device = NULL;

    g_free(gva_track->tracking_config);
    gva_track->tracking_config = NULL;

    g_free(gva_track->feature_model);
    gva_track->feature_model = NULL;

    if (gva_track->info) {
        gst_video_info_free(gva_track->info);
        gva_track->info = NULL;
    }
}

static void gst_gva_track_class_init(GstGvaTrackClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_gva_track_set_property);
    gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_gva_track_get_property);
    gobject_class->dispose = GST_DEBUG_FUNCPTR(gst_gva_track_dispose);
    gobject_class->finalize = GST_DEBUG_FUNCPTR(gst_gva_track_finalize);

    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    base_transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_gva_track_set_caps);
    base_transform_class->sink_event = GST_DEBUG_FUNCPTR(gst_gva_track_sink_event);
    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_gva_track_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_gva_track_stop);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gva_track_transform_ip);

    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    element_class->change_state = GST_DEBUG_FUNCPTR(gst_gva_track_change_state);

    gst_element_class_set_static_metadata(element_class, ELEMENT_LONG_NAME, "video", ELEMENT_DESCRIPTION,
                                          "Intel Corporation");

    gst_element_class_add_pad_template(
        element_class, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_from_string(GVA_CAPS)));
    gst_element_class_add_pad_template(
        element_class, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string(GVA_CAPS)));

    const auto kDefaultGParamFlags =
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
    g_object_class_install_property(
        gobject_class, PROP_TRACKING_TYPE,
        g_param_spec_enum("tracking-type", "TrackingType",
                          "Tracking algorithm used to identify the same object in multiple frames. "
                          "Please see user guide for more details",
                          GST_GVA_TRACKING_TYPE, ZERO_TERM, kDefaultGParamFlags));
    g_object_class_install_property(
        gobject_class, PROP_DEVICE,
        g_param_spec_string("device", "Device", "Target device for tracking. This version supports only CPU device", "",
                            kDefaultGParamFlags));
    g_object_class_install_property(gobject_class, PROP_TRACKING_CONFIG,
                                    g_param_spec_string("config", "Tracker specific configuration",
                                                        "Comma separated list of KEY=VALUE parameters specific to "
                                                        "platform/tracker. Please see user guide for more details",
                                                        nullptr, kDefaultGParamFlags));
    g_object_class_install_property(gobject_class, PROP_DEEPSORT_TRCK_CFG,
                                    g_param_spec_string("deepsort-trck-cfg", "Deep SORT tracker configuration",
                                                        "Comma separated list of KEY=VALUE parameters specific to."
                                                        "Please see user guide for more details"
                                                        "Deep SORT tracker: max_iou_distance (default 0.7), "
                                                        "max_age (default 30 frames), n_init (default 3 frames), "
                                                        "max_cosine_distance (default 0.2), nn_budget (default 100). "
                                                        "Example: deepsort-trck-cfg=max_age=60,max_cosine_distance=0.3",
                                                        nullptr, kDefaultGParamFlags));
}

static void gst_gva_track_init(GstGvaTrack *gva_track) {
    GST_DEBUG_OBJECT(gva_track, "gst_gva_track_init");
}

static void gst_gva_track_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaTrack *gva_track = GST_GVA_TRACK(object);
    GST_DEBUG_OBJECT(gva_track, "gst_gva_track_set_property %d", prop_id);

    switch (prop_id) {
    case PROP_DEVICE:
        g_free(gva_track->device);
        gva_track->device = g_ascii_strup(g_value_dup_string(value), -1);
        break;
    case PROP_TRACKING_TYPE:
        gva_track->tracking_type = static_cast<GstGvaTrackingType>(g_value_get_enum(value));
        break;
    case PROP_TRACKING_CONFIG:
        g_free(gva_track->tracking_config);
        gva_track->tracking_config = g_value_dup_string(value);
        break;
    case PROP_DEEPSORT_TRCK_CFG:
        g_free(gva_track->deepsort_trck_cfg);
        gva_track->deepsort_trck_cfg = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gva_track_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaTrack *gva_track = GST_GVA_TRACK(object);
    GST_DEBUG_OBJECT(gva_track, "gst_gva_track_get_property %d", prop_id);

    switch (prop_id) {
    case PROP_DEVICE:
        g_value_set_string(value, gva_track->device);
        break;
    case PROP_TRACKING_TYPE:
        g_value_set_enum(value, gva_track->tracking_type);
        break;
    case PROP_TRACKING_CONFIG:
        g_value_set_string(value, gva_track->tracking_config);
        break;
    case PROP_DEEPSORT_TRCK_CFG:
        g_value_set_string(value, gva_track->deepsort_trck_cfg);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

void gst_gva_track_dispose(GObject *object) {
    GstGvaTrack *gva_track = GST_GVA_TRACK(object);

    GST_DEBUG_OBJECT(gva_track, "gst_gva_track_dispose");

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS(gst_gva_track_parent_class)->dispose(object);
}

void gst_gva_track_finalize(GObject *object) {
    GstGvaTrack *gva_track = GST_GVA_TRACK(object);
    GST_DEBUG_OBJECT(gva_track, "gst_gva_track_finalize");

    gst_gva_track_cleanup(gva_track);

    G_OBJECT_CLASS(gst_gva_track_parent_class)->finalize(object);
}

static GstStateChangeReturn gst_gva_track_change_state(GstElement *element, GstStateChange transition) {
    GstGvaTrack *gva_track = GST_GVA_TRACK(element);

    GstStateChangeReturn ret = GST_ELEMENT_CLASS(gst_gva_track_parent_class)->change_state(element, transition);
    GST_DEBUG_OBJECT(gva_track, "GstStateChangeReturn: %s", gst_element_state_change_return_get_name(ret));

    return ret;
}

static gboolean _check_device_correctness(GstGvaTrack *gva_track) {
    return strcmp(gva_track->device, DEVICE_GPU) == 0 && gva_track->caps_feature != VA_SURFACE_CAPS_FEATURE &&
           gva_track->caps_feature != DMA_BUF_CAPS_FEATURE && gva_track->caps_feature != VA_MEMORY_CAPS_FEATURE;
}

static gboolean _check_device_capabilities(GstGvaTrack *gva_track) {
    if (g_strcmp0(gva_track->device, DEVICE_GPU) == 0) {
        return gva_track->tracking_type == ZERO_TERM;
    }
    return TRUE;
}

static void _try_to_create_default_gpu_tracker(GstGvaTrack *gva_track) {
    // Set default device if it wasn't specified by user
    if (gva_track->device != NULL && gva_track->device[0] != '\0')
        return;
    // gboolean tryGPU = gva_track->tracking_type == ZERO_TERM && (gva_track->caps_feature == VA_SURFACE_CAPS_FEATURE ||
    //                                                             gva_track->caps_feature == VA_MEMORY_CAPS_FEATURE ||
    //                                                             gva_track->caps_feature == DMA_BUF_CAPS_FEATURE);
    // tryGPU = FALSE; // disable default loading of libvasot_gpu, will be removed
    // if (tryGPU) {
    //     gva_track->device = g_strdup(DEVICE_GPU);
    //     init_tracker_obj(gva_track);
    // }

    if (gva_track->tracker == NULL)
        gva_track->device = g_strdup(DEVICE_CPU);
}

static gboolean gst_gva_track_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    UNUSED(outcaps);

    GstGvaTrack *gva_track = GST_GVA_TRACK(trans);
    GST_DEBUG_OBJECT(gva_track, "gst_gva_track_set_caps");

    if (!gva_track->info) {
        gva_track->info = gst_video_info_new();
    }
    gst_video_info_from_caps(gva_track->info, incaps);
    release_tracker_obj(gva_track);
    gva_track->caps_feature = get_caps_feature(incaps);

    _try_to_create_default_gpu_tracker(gva_track);

    if (_check_device_correctness(gva_track)) {
        GST_ELEMENT_ERROR(gva_track, LIBRARY, INIT, ("tracker intitialization failed"),
                          ("memory type should be VASurface or DMABuf for running on GPU"));
        return FALSE;
    }

    if (!(_check_device_capabilities(gva_track))) {
        GST_ELEMENT_ERROR(gva_track, LIBRARY, INIT, ("tracker intitialization failed"),
                          ("Only zero-term tracker type is supported for running on GPU"));
        return FALSE;
    }

    if (gva_track->tracker == NULL) {
        if (!init_tracker_obj(gva_track)) {
            GST_ELEMENT_ERROR(gva_track, LIBRARY, INIT, ("tracker intitialization failed"), (nullptr));
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean gst_gva_track_sink_event(GstBaseTransform *trans, GstEvent *event) {
    GstGvaTrack *gva_track = GST_GVA_TRACK(trans);

    GST_DEBUG_OBJECT(gva_track, "sink_event %s", GST_EVENT_TYPE_NAME(event));

    return GST_BASE_TRANSFORM_CLASS(gst_gva_track_parent_class)->sink_event(trans, event);
}

static gboolean gst_gva_track_start(GstBaseTransform *trans) {
    GstGvaTrack *gva_track = GST_GVA_TRACK(trans);

    if (gva_track->tracking_type == DEEP_SORT) {
        GST_INFO_OBJECT(gva_track, "Deep SORT configuration:\n Deep SORT config: %s\n",
                        gva_track->deepsort_trck_cfg ? gva_track->deepsort_trck_cfg : "using defaults");
    } else {
        GST_INFO_OBJECT(gva_track, "%s parameters:\n -- Device: %s\n -- Tracking type: %s\n -- Tracking config: %s\n",
                        GST_ELEMENT_NAME(GST_ELEMENT_CAST(gva_track)), gva_track->device,
                        tracking_type_to_string(gva_track->tracking_type), gva_track->tracking_config);
    }

    return TRUE;
}

static gboolean gst_gva_track_stop(GstBaseTransform *trans) {
    UNUSED(trans);
    return TRUE;
}

static GstFlowReturn gst_gva_track_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstGvaTrack *gva_track = GST_GVA_TRACK(trans);
    GstFlowReturn status = GST_FLOW_OK;
    if (gva_track && gva_track->tracker) {
        try {
            auto gstbuffer = std::make_shared<dlstreamer::GSTFrame>(buf, gva_track->info);
            GVA::VideoFrame video_frame(buf, gva_track->info);

            gva_track->tracker->track(gstbuffer, video_frame);
        } catch (const std::exception &e) {
            GST_ELEMENT_ERROR(gva_track, STREAM, FAILED, ("transform_ip failed"),
                              ("%s", Utils::createNestedErrorMsg(e).c_str()));
            status = GST_FLOW_ERROR;
        }
    } else {
        GST_ELEMENT_ERROR(gva_track, STREAM, FAILED, ("transform_ip failed"), ("%s", "bad argument gva_track"));
        status = GST_FLOW_ERROR;
    }
    return status;
}
