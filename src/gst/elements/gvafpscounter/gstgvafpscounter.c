/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvafpscounter.h"

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

#include "fpscounter_c.h"

#include "config.h"
#include "utils.h"

#define ELEMENT_LONG_NAME "Frames Per Second counter"
#define ELEMENT_DESCRIPTION "Measures frames per second across multiple streams in a single process."

#define CAPS_TEMPLATE_STRING "video/x-raw(ANY);other/tensors(ANY)"
static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(CAPS_TEMPLATE_STRING));

GST_DEBUG_CATEGORY_STATIC(gst_gva_fpscounter_debug_category);
#define GST_CAT_DEFAULT gst_gva_fpscounter_debug_category

enum {
    PROP_0,
    PROP_INTERVAL,
    PROP_STARTING_FRAME,
    PROP_WRITE_PIPE,
    PROP_READ_PIPE,
    PROP_PRINT_STD_DEV,
    PROP_PRINT_LATENCY,
    PROP_AVG_FPS,
    PROP_DETECTIONS
};

#define DEFAULT_INTERVAL "1"

#define DEFAULT_STARTING_FRAME 0
#define DEFAULT_MIN_STARTING_FRAME 0
#define DEFAULT_MAX_STARTING_FRAME UINT_MAX
#define DEFAULT_PRINT_STD_DEV 0
#define DEFAULT_PRINT_LATENCY 0
#define DEFAULT_AVG_FPS 0.0
#define DEFAULT_AVG_FPS_MIN 0.0
#define DEFAULT_AVG_FPS_MAX G_MAXFLOAT
#define DEFAULT_DETECTIONS 0
#define DEFAULT_MIN_DETECTIONS 0
#define DEFAULT_MAX_DETECTIONS UINT_MAX

/* prototypes */
static void gst_gva_fpscounter_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_gva_fpscounter_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static gboolean gst_gva_fpscounter_check_interval_value(const GValue *value);
static gboolean gst_gva_fpscounter_start(GstBaseTransform *trans);
static void gst_gva_fpscounter_dispose(GObject *object);
static void gst_gva_fpscounter_finalize(GObject *object);
static void gst_gva_fpscounter_cleanup(GstGvaFpscounter *gva_fpscounter);

static GstFlowReturn gst_gva_fpscounter_transform_ip(GstBaseTransform *trans, GstBuffer *buf);
static gboolean gst_gva_fpscounter_sink_event(GstBaseTransform *trans, GstEvent *event);

/* class initialization */
static void gst_gva_fpscounter_init(GstGvaFpscounter *gva_fpscounter);

G_DEFINE_TYPE_WITH_CODE(GstGvaFpscounter, gst_gva_fpscounter, GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gst_gva_fpscounter_debug_category, "gvafpscounter", 0,
                                                "debug category for gvafpscounter element"));

static void gst_gva_fpscounter_class_init(GstGvaFpscounterClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    /* Setting up pads and setting metadata should be moved to
       base_class_init if you intend to subclass this class. */
    gst_element_class_add_static_pad_template(element_class, &srctemplate);
    gst_element_class_add_static_pad_template(element_class, &sinktemplate);

    gst_element_class_set_static_metadata(element_class, ELEMENT_LONG_NAME, "Video", ELEMENT_DESCRIPTION,
                                          "Intel Corporation");
    gobject_class->set_property = gst_gva_fpscounter_set_property;
    gobject_class->get_property = gst_gva_fpscounter_get_property;
    gobject_class->dispose = gst_gva_fpscounter_dispose;
    gobject_class->finalize = gst_gva_fpscounter_finalize;
    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_gva_fpscounter_start);
    base_transform_class->transform = NULL;
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gva_fpscounter_transform_ip);
    base_transform_class->sink_event = GST_DEBUG_FUNCPTR(gst_gva_fpscounter_sink_event);

    g_object_class_install_property(
        gobject_class, PROP_INTERVAL,
        g_param_spec_string("interval", "Interval", "The time interval in seconds for which the fps will be measured",
                            DEFAULT_INTERVAL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(
        gobject_class, PROP_STARTING_FRAME,
        g_param_spec_uint("starting-frame", "Starting frame",
                          "Start collecting fps measurements after the specified number of frames have been "
                          "processed to remove the influence of initialization cost",
                          DEFAULT_MIN_STARTING_FRAME, DEFAULT_MAX_STARTING_FRAME, DEFAULT_STARTING_FRAME,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_WRITE_PIPE,
        g_param_spec_string("write-pipe", "Write into named pipe",
                            "Write FPS data to a named pipe. Blocks until read-pipe is opened.", "",
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(
        gobject_class, PROP_READ_PIPE,
        g_param_spec_string("read-pipe", "Read from named pipe",
                            "Read FPS data from a named pipe. Create and delete a named pipe.", "",
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, PROP_PRINT_STD_DEV,
                                    g_param_spec_boolean("print-std-dev", "print-std-dev",
                                                         "If true, prints standard deviations", DEFAULT_PRINT_STD_DEV,
                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, PROP_PRINT_LATENCY,
                                    g_param_spec_boolean("print-latency", "print-latency",
                                                         "If true, prints average frame latency", DEFAULT_PRINT_LATENCY,
                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_AVG_FPS,
                                    g_param_spec_float("avg-fps", "Average FPS",
                                                       "The average frames per second, read-only parameter",
                                                       DEFAULT_AVG_FPS_MIN, DEFAULT_AVG_FPS_MAX, DEFAULT_AVG_FPS,
                                                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, PROP_DETECTIONS,
                                    g_param_spec_uint("detections", "For internal use only", "For internal use only",
                                                      DEFAULT_MIN_DETECTIONS, DEFAULT_MAX_DETECTIONS,
                                                      DEFAULT_DETECTIONS, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void gst_gva_fpscounter_init(GstGvaFpscounter *gva_fpscounter) {
    GST_DEBUG_OBJECT(gva_fpscounter, "gva_fpscounter_init");

    gst_gva_fpscounter_cleanup(gva_fpscounter);

    gva_fpscounter->interval = g_strdup(DEFAULT_INTERVAL);
    gva_fpscounter->starting_frame = DEFAULT_STARTING_FRAME;
    gva_fpscounter->write_pipe = NULL;
    gva_fpscounter->read_pipe = NULL;
    gva_fpscounter->print_std_dev = DEFAULT_PRINT_STD_DEV;
    gva_fpscounter->print_latency = DEFAULT_PRINT_LATENCY;
    gva_fpscounter->avg_fps = DEFAULT_AVG_FPS;
    gva_fpscounter->detections = DEFAULT_DETECTIONS;
}

void gst_gva_fpscounter_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GstGvaFpscounter *gvafpscounter = GST_GVA_FPSCOUNTER(object);

    GST_DEBUG_OBJECT(gvafpscounter, "get_property");
    switch (property_id) {
    case PROP_INTERVAL:
        g_value_set_string(value, gvafpscounter->interval);
        break;
    case PROP_STARTING_FRAME:
        g_value_set_uint(value, gvafpscounter->starting_frame);
        break;
    case PROP_WRITE_PIPE:
        g_value_set_string(value, gvafpscounter->write_pipe);
        break;
    case PROP_READ_PIPE:
        g_value_set_string(value, gvafpscounter->read_pipe);
        break;
    case PROP_PRINT_STD_DEV:
        g_value_set_boolean(value, gvafpscounter->print_std_dev);
        break;
    case PROP_PRINT_LATENCY:
        g_value_set_boolean(value, gvafpscounter->print_latency);
        break;
    case PROP_AVG_FPS:
        g_value_set_float(value, gvafpscounter->avg_fps);
        break;
    case PROP_DETECTIONS:
        g_value_set_uint(value, gvafpscounter->detections);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gboolean gst_gva_fpscounter_check_interval_value(const GValue *value) {
    /* check if interval property is valid */
    if (!fps_counter_validate_intervals(g_value_get_string(value))) {
        g_warning("gvafpscounter's interval property value is invalid."
                  " Positive integers must be used (may be comma separated). Default value will be set.");
        return FALSE;
    }

    return TRUE;
}

void gst_gva_fpscounter_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    GstGvaFpscounter *gvafpscounter = GST_GVA_FPSCOUNTER(object);

    GST_DEBUG_OBJECT(gvafpscounter, "set_property");
    switch (property_id) {
    case PROP_INTERVAL:
        g_free(gvafpscounter->interval);
        if (gst_gva_fpscounter_check_interval_value(value)) {
            gvafpscounter->interval = g_value_dup_string(value);
        } else {
            gvafpscounter->interval = g_strdup(DEFAULT_INTERVAL);
        }
        break;
    case PROP_STARTING_FRAME:
        gvafpscounter->starting_frame = g_value_get_uint(value);
        break;
    case PROP_WRITE_PIPE:
        g_free(gvafpscounter->write_pipe);
        gvafpscounter->write_pipe = g_value_dup_string(value);
        break;
    case PROP_READ_PIPE:
        g_free(gvafpscounter->read_pipe);
        gvafpscounter->read_pipe = g_value_dup_string(value);
        break;
    case PROP_PRINT_STD_DEV:
        gvafpscounter->print_std_dev = g_value_get_boolean(value);
        break;
    case PROP_PRINT_LATENCY:
        gvafpscounter->print_latency = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void gst_gva_fpscounter_cleanup(GstGvaFpscounter *gva_fpscounter) {
    if (gva_fpscounter == NULL)
        return;

    GST_DEBUG_OBJECT(gva_fpscounter, "gst_gva_fpscounter_cleanup");

    if (gva_fpscounter->interval) {
        g_free(gva_fpscounter->interval);
        gva_fpscounter->interval = NULL;
    }
    g_free(gva_fpscounter->write_pipe);
    g_free(gva_fpscounter->read_pipe);
}

static gboolean gst_gva_fpscounter_start(GstBaseTransform *trans) {
    GstGvaFpscounter *gvafpscounter = GST_GVA_FPSCOUNTER(trans);

    GST_DEBUG_OBJECT(gvafpscounter, "start");

    GST_INFO_OBJECT(gvafpscounter, "%s parameters:\n -- Starting frame: %d\n -- Interval: %s\n",
                    GST_ELEMENT_NAME(GST_ELEMENT_CAST(gvafpscounter)), gvafpscounter->starting_frame,
                    gvafpscounter->interval);

    if (gvafpscounter->write_pipe) {
        fps_counter_create_writepipe(gvafpscounter->write_pipe);
    } else {
        fps_counter_create_average(gvafpscounter->starting_frame, 1);
        fps_counter_create_iterative(gvafpscounter->interval, gvafpscounter->print_std_dev,
                                     gvafpscounter->print_latency);
        if (gvafpscounter->read_pipe) {
            fps_counter_create_readpipe(gvafpscounter, gvafpscounter->read_pipe);
        }
    }
    return TRUE;
}

gboolean gst_gva_fpscounter_sink_event(GstBaseTransform *trans, GstEvent *event) {
    UNUSED(trans);

    if (event->type == GST_EVENT_EOS) {
        fps_counter_eos(GST_ELEMENT_NAME(GST_ELEMENT(trans)));
    }

    return GST_BASE_TRANSFORM_CLASS(gst_gva_fpscounter_parent_class)->sink_event(trans, event);
}

void gst_gva_fpscounter_dispose(GObject *object) {
    GstGvaFpscounter *gvafpscounter = GST_GVA_FPSCOUNTER(object);
    GST_DEBUG_OBJECT(gvafpscounter, "dispose");

    fps_counter_eos(GST_ELEMENT_NAME(object));

    G_OBJECT_CLASS(gst_gva_fpscounter_parent_class)->dispose(object);
}

void gst_gva_fpscounter_finalize(GObject *object) {
    GstGvaFpscounter *gvafpscounter = GST_GVA_FPSCOUNTER(object);
    GST_DEBUG_OBJECT(gvafpscounter, "finalize");

    fps_counter_eos(GST_ELEMENT_NAME(object));
    gst_gva_fpscounter_cleanup(gvafpscounter);

    G_OBJECT_CLASS(gst_gva_fpscounter_parent_class)->finalize(object);
}

static GstFlowReturn gst_gva_fpscounter_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstGvaFpscounter *gvafpscounter = GST_GVA_FPSCOUNTER(trans);

    GST_DEBUG_OBJECT(gvafpscounter, "transform_ip");

    fps_counter_new_frame(buf, GST_ELEMENT_NAME(GST_ELEMENT(trans)), gvafpscounter);

    if (!gst_pad_is_linked(GST_BASE_TRANSFORM_SRC_PAD(trans))) {
        return GST_BASE_TRANSFORM_FLOW_DROPPED;
    }

    return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin) {
    if (!gst_element_register(plugin, "gvafpscounter", GST_RANK_NONE, GST_TYPE_GVA_FPSCOUNTER))
        return FALSE;

    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, gvafpscounter, PRODUCT_FULL_NAME " gvafpscounter element",
                  plugin_init, PLUGIN_VERSION, PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
