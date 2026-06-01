/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvaattachroi.h"
#include "attachroi.h"
#include "gva_caps.h"
#include "utils.h"

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC(gstgvaattachroigst_gva_attach_roi_debug_category);
#define GST_CAT_DEFAULT gstgvaattachroigst_gva_attach_roi_debug_category

#define ELEMENT_LONG_NAME "Generic ROI metadata generator"
#define ELEMENT_DESCRIPTION "Generic ROI metadata generator"

/* prototypes */

static void gst_gva_attach_roi_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_gva_attach_roi_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gst_gva_attach_roi_dispose(GObject *object);
static void gst_gva_attach_roi_finalize(GObject *object);

static gboolean gst_gva_attach_roi_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);

static gboolean gst_gva_attach_roi_start(GstBaseTransform *trans);
static gboolean gst_gva_attach_roi_stop(GstBaseTransform *trans);
static gboolean gst_gva_attach_roi_sink_event(GstBaseTransform *trans, GstEvent *event);

static void gst_gva_attach_roi_before_transform(GstBaseTransform *trans, GstBuffer *buffer);
static GstFlowReturn gst_gva_attach_roi_transform_ip(GstBaseTransform *trans, GstBuffer *buf);
static void gst_gva_attach_roi_cleanup(GstGvaAttachRoi *gvaattachroi);
static void gst_gva_attach_roi_reset(GstGvaAttachRoi *gvaattachroi);

namespace {

constexpr auto DEFAULT_FILE_PATH = nullptr;
constexpr auto DEFAULT_MODE = static_cast<guint>(AttachRoi::Mode::InOrder);
constexpr auto DEFAULT_ROI = nullptr;

constexpr auto UNKNOWN_VALUE_NAME = "unknown";
constexpr auto MODE_IN_ORDER_NAME = "in-order";
constexpr auto MODE_IN_LOOP_NAME = "in-loop";
constexpr auto MODE_BY_TIMESTAMP_NAME = "by-timestamp";

std::string mode_to_string(gint mode) {
    auto attach_mode = static_cast<AttachRoi::Mode>(mode);
    switch (attach_mode) {
    case AttachRoi::Mode::InOrder:
        return MODE_IN_ORDER_NAME;
    case AttachRoi::Mode::InLoop:
        return MODE_IN_LOOP_NAME;
    case AttachRoi::Mode::ByTimestamp:
        return MODE_BY_TIMESTAMP_NAME;
    default:
        return UNKNOWN_VALUE_NAME;
    }
}

} // namespace

enum {
    PROP_0,
    PROP_FILE_PATH,
    PROP_MODE,
    PROP_ROI,
};

#define GST_GVA_ATTACH_ROI_MODE (gst_gva_attach_roi_mode_get_type())
static GType gst_gva_attach_roi_mode_get_type() {
    static gchar order_desc[] =
        "Attach ROIs in order. Number of frames in the pipeline must match to number of ROIs in JSON.";
    static gchar loop_desc[] = "Attach ROIs in cyclic manner. Same as in-order, but for cases when the number of "
                               "frames in the pipeline exceeds ROIs in JSON.";
    static gchar ts_desc[] = "Attach ROIs using timestamping. ROIs in JSON file must be timestamped.";
    static const GEnumValue modes[] = {
        {static_cast<guint>(AttachRoi::Mode::InOrder), order_desc, MODE_IN_ORDER_NAME},
        {static_cast<guint>(AttachRoi::Mode::InLoop), loop_desc, MODE_IN_LOOP_NAME},
        {static_cast<guint>(AttachRoi::Mode::ByTimestamp), ts_desc, MODE_BY_TIMESTAMP_NAME},
        {0, NULL, NULL}};
    static GType attach_mode_type = g_enum_register_static("GstGVAAttachRoiMode", modes);
    return attach_mode_type;
}

/* class initialization */

G_DEFINE_TYPE_WITH_CODE(GstGvaAttachRoi, gst_gva_attach_roi, GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gstgvaattachroigst_gva_attach_roi_debug_category, "gvaattachroi", 0,
                                                "debug category for gvaattachroi element"));

static void gst_gva_attach_roi_class_init(GstGvaAttachRoiClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    gst_element_class_add_pad_template(
        GST_ELEMENT_CLASS(klass),
        gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_from_string(GVA_CAPS)));
    gst_element_class_add_pad_template(
        GST_ELEMENT_CLASS(klass),
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string(GVA_CAPS)));

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass), ELEMENT_LONG_NAME, "Metadata", ELEMENT_DESCRIPTION,
                                          "Intel Corporation");

    gobject_class->set_property = gst_gva_attach_roi_set_property;
    gobject_class->get_property = gst_gva_attach_roi_get_property;

    gobject_class->dispose = gst_gva_attach_roi_dispose;
    gobject_class->finalize = gst_gva_attach_roi_finalize;

    base_transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_gva_attach_roi_set_caps);

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_gva_attach_roi_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_gva_attach_roi_stop);

    base_transform_class->sink_event = GST_DEBUG_FUNCPTR(gst_gva_attach_roi_sink_event);

    base_transform_class->before_transform = GST_DEBUG_FUNCPTR(gst_gva_attach_roi_before_transform);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gva_attach_roi_transform_ip);

    const GParamFlags gparam_flags = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_FILE_PATH,
                                    g_param_spec_string("file-path", "FilePath",
                                                        "Absolute path to input file with ROIs to attach to buffer.",
                                                        DEFAULT_FILE_PATH, gparam_flags));

    g_object_class_install_property(gobject_class, PROP_MODE,
                                    g_param_spec_enum("mode", "AttachMode", "Mode used to attach ROIs from JSON file",
                                                      GST_GVA_ATTACH_ROI_MODE, DEFAULT_MODE, gparam_flags));

    g_object_class_install_property(
        gobject_class, PROP_ROI,
        g_param_spec_string(
            "roi", "Region of Interest",
            "Specifies pixel absolute coordinates of ROI to attach to buffer in form: " ROI_FORMAT_STRING, DEFAULT_ROI,
            gparam_flags));
}

static void gst_gva_attach_roi_init(GstGvaAttachRoi *gvaattachroi) {

    GST_DEBUG_OBJECT(gvaattachroi, "gst_gva_attach_roi_init");
    GST_DEBUG_OBJECT(gvaattachroi, "%s", GST_ELEMENT_NAME(GST_ELEMENT(gvaattachroi)));
    gvaattachroi->info = NULL;

    gst_gva_attach_roi_reset(gvaattachroi);
}

void gst_gva_attach_roi_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    GstGvaAttachRoi *gvaattachroi = GST_GVA_ATTACH_ROI(object);

    GST_DEBUG_OBJECT(gvaattachroi, "set_property %d", property_id);

    switch (property_id) {
    case PROP_FILE_PATH:
        g_free(gvaattachroi->filepath);
        gvaattachroi->filepath = g_value_dup_string(value);
        break;
    case PROP_MODE:
        gvaattachroi->mode = g_value_get_enum(value);
        break;
    case PROP_ROI:
        g_free(gvaattachroi->roi_prop);
        gvaattachroi->roi_prop = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_gva_attach_roi_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GstGvaAttachRoi *gvaattachroi = GST_GVA_ATTACH_ROI(object);

    GST_DEBUG_OBJECT(gvaattachroi, "get_property");

    switch (property_id) {
    case PROP_FILE_PATH:
        g_value_set_string(value, gvaattachroi->filepath);
        break;
    case PROP_MODE:
        g_value_set_enum(value, gvaattachroi->mode);
        break;
    case PROP_ROI:
        g_value_set_string(value, gvaattachroi->roi_prop);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_gva_attach_roi_dispose(GObject *object) {
    GstGvaAttachRoi *gvaattachroi = GST_GVA_ATTACH_ROI(object);

    GST_DEBUG_OBJECT(gvaattachroi, "dispose");

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS(gst_gva_attach_roi_parent_class)->dispose(object);
}

void gst_gva_attach_roi_finalize(GObject *object) {
    GstGvaAttachRoi *gvaattachroi = GST_GVA_ATTACH_ROI(object);

    GST_DEBUG_OBJECT(gvaattachroi, "finalize");

    /* clean up object here */
    gst_gva_attach_roi_cleanup(gvaattachroi);

    G_OBJECT_CLASS(gst_gva_attach_roi_parent_class)->finalize(object);
}

static void gst_gva_attach_roi_cleanup(GstGvaAttachRoi *gvaattachroi) {
    if (!gvaattachroi)
        return;

    GST_DEBUG_OBJECT(gvaattachroi, "gst_gva_attach_roi_cleanup");

    if (gvaattachroi->impl)
        delete gvaattachroi->impl;
    gvaattachroi->impl = nullptr;

    g_free(gvaattachroi->filepath);
    gvaattachroi->filepath = nullptr;

    g_free(gvaattachroi->roi_prop);
    gvaattachroi->roi_prop = nullptr;

    if (gvaattachroi->info) {
        gst_video_info_free(gvaattachroi->info);
        gvaattachroi->info = nullptr;
    }
}

static void gst_gva_attach_roi_reset(GstGvaAttachRoi *gvaattachroi) {
    GST_DEBUG_OBJECT(gvaattachroi, "gst_gva_attach_roi_reset");

    if (gvaattachroi == nullptr)
        return;

    gst_gva_attach_roi_cleanup(gvaattachroi);

    gvaattachroi->filepath = DEFAULT_FILE_PATH;
    gvaattachroi->mode = DEFAULT_MODE;
    gvaattachroi->roi_prop = DEFAULT_ROI;
}

static gboolean gst_gva_attach_roi_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    UNUSED(incaps);
    UNUSED(outcaps);

    GstGvaAttachRoi *self = GST_GVA_ATTACH_ROI(trans);
    GST_DEBUG_OBJECT(self, "set_caps");

    if (!self->info)
        self->info = gst_video_info_new();

    gst_video_info_from_caps(self->info, incaps);

    return true;
}

/* states */
static gboolean gst_gva_attach_roi_start(GstBaseTransform *trans) {
    GstGvaAttachRoi *self = GST_GVA_ATTACH_ROI(trans);

    GST_DEBUG_OBJECT(self, "start");

    GST_INFO_OBJECT(self,
                    "%s parameters:\n -- File path: %s\n -- Mode: %s\n "
                    "-- ROI: %s\n",
                    GST_ELEMENT_NAME(GST_ELEMENT_CAST(self)), self->filepath, mode_to_string(self->mode).c_str(),
                    self->roi_prop);

    try {
        self->impl = new AttachRoi(self->filepath, self->roi_prop, static_cast<AttachRoi::Mode>(self->mode));
        if (!self->impl)
            return false;
    } catch (std::exception &e) {
        GST_ELEMENT_ERROR(self, RESOURCE, SETTINGS, ("%s", "Couldn't start!"),
                          ("Reason: %s", Utils::createNestedErrorMsg(e).c_str()));
        return false;
    }

    return true;
}

static gboolean gst_gva_attach_roi_stop(GstBaseTransform *trans) {
    GstGvaAttachRoi *gvaattachroi = GST_GVA_ATTACH_ROI(trans);

    GST_DEBUG_OBJECT(gvaattachroi, "stop");

    // free(gvaattachroi->file_content);
    // fclose(gvaattachroi->file);

    gst_gva_attach_roi_reset(gvaattachroi);

    // free(gvaattachroi->line_buf);

    return TRUE;
}

/* sink and src pad event handlers */
static gboolean gst_gva_attach_roi_sink_event(GstBaseTransform *trans, GstEvent *event) {
    GstGvaAttachRoi *gvaattachroi = GST_GVA_ATTACH_ROI(trans);

    GST_DEBUG_OBJECT(gvaattachroi, "sink_event");

    return GST_BASE_TRANSFORM_CLASS(gst_gva_attach_roi_parent_class)->sink_event(trans, event);
}

static void gst_gva_attach_roi_before_transform(GstBaseTransform *trans, GstBuffer *buffer) {
    GstGvaAttachRoi *gvaattachroi = GST_GVA_ATTACH_ROI(trans);

    GST_DEBUG_OBJECT(gvaattachroi, "before transform");
    GstClockTime timestamp;

    timestamp = gst_segment_to_stream_time(&trans->segment, GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP(buffer));
    GST_LOG_OBJECT(gvaattachroi, "Got stream time of %d" GST_TIME_FORMAT, GST_TIME_ARGS(timestamp));
    if (GST_CLOCK_TIME_IS_VALID(timestamp))
        gst_object_sync_values(GST_OBJECT(trans), timestamp);
}

static GstFlowReturn gst_gva_attach_roi_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstGvaAttachRoi *self = GST_GVA_ATTACH_ROI(trans);

    GstClockTime ts = gst_segment_to_stream_time(&self->base_gvaattachroi.segment, GST_FORMAT_TIME, buf->pts);

    try {
        g_assert(self->impl);
        GVA::VideoFrame vf(buf, self->info);
        self->impl->attachMetas(vf, buf, ts);
    } catch (std::exception &e) {
        GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("%s", "Error attaching meta!"),
                          ("Reason: %s", Utils::createNestedErrorMsg(e).c_str()));
        return GST_FLOW_ERROR;
    }

    return GST_FLOW_OK;
}
