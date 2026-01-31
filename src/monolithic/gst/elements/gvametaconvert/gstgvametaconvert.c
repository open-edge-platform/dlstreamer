/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvametaconvert.h"

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "config.h"
#include "converters.h"
#include "gva_caps.h"
#include "gva_json_meta.h"
#include "utils.h"

#define ELEMENT_LONG_NAME "Metadata converter"
#define ELEMENT_DESCRIPTION "Converts the metadata structure to the JSON format."

GST_DEBUG_CATEGORY(gst_gva_meta_convert_debug_category);
#define GST_CAT_DEFAULT gst_gva_meta_convert_debug_category
/* prototypes */

static void gst_gva_meta_convert_set_property(GObject *object, guint property_id, const GValue *value,
                                              GParamSpec *pspec);
static void gst_gva_meta_convert_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gst_gva_meta_convert_dispose(GObject *object);
static void gst_gva_meta_convert_finalize(GObject *object);

static gboolean gst_gva_meta_convert_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);
static gboolean gst_gva_meta_convert_start(GstBaseTransform *trans);
static gboolean gst_gva_meta_convert_stop(GstBaseTransform *trans);
static gboolean gst_gva_meta_convert_sink_event(GstBaseTransform *trans, GstEvent *event);
static GstFlowReturn gst_gva_meta_convert_transform_ip(GstBaseTransform *trans, GstBuffer *buf);
static void gst_gva_meta_convert_cleanup(GstGvaMetaConvert *gvametaconvert);
static void gst_gva_meta_convert_reset(GstGvaMetaConvert *gvametaconvert);
static GstStateChangeReturn gst_gva_meta_convert_change_state(GstElement *element, GstStateChange transition);

enum {
    SIGNAL_HANDOFF,
    /* FILL ME */
    LAST_SIGNAL
};
static guint gst_interpret_signals[LAST_SIGNAL] = {0};

#define DEFAULT_FORMAT GST_GVA_METACONVERT_JSON
#define DEFAULT_SIGNAL_HANDOFFS FALSE
#define DEFAULT_ADD_TENSOR_DATA FALSE
#define DEFAULT_TIMESTAMP_UTC FALSE
#define DEFAULT_TIMESTAMP_MICROSECONDS FALSE
#define DEFAULT_SOURCE NULL
#define DEFAULT_TAGS NULL
#define DEFAULT_ADD_EMPTY_DETECTION_RESULTS FALSE
#define DEFAULT_JSON_INDENT -1
#define MIN_JSON_INDENT -1
#define MAX_JSON_INDENT 10

// Enum value names
#define UNKNOWN_VALUE_NAME "unknown"

#define FORMAT_JSON_NAME "json"
#define FORMAT_DUMP_DETECTION_NAME "dump-detection"

enum {
    PROP_0,
    PROP_FORMAT,
    PROP_ADD_TENSOR_DATA,
    PROP_SIGNAL_HANDOFFS,
    PROP_SOURCE,
    PROP_TAGS,
    PROP_ADD_EMPTY_DETECTION_RESULTS,
    PROP_JSON_INDENT,
    PROP_TIMESTAMP_UTC,
    PROP_TIMESTAMP_MICROSECONDS
};

static const gchar *format_type_to_string(GstGVAMetaconvertFormatType format) {
    switch (format) {
    case GST_GVA_METACONVERT_JSON:
        return FORMAT_JSON_NAME;
    case GST_GVA_METACONVERT_DUMP_DETECTION:
        return FORMAT_DUMP_DETECTION_NAME;
    default:
        return UNKNOWN_VALUE_NAME;
    }
}

/* class initialization */

G_DEFINE_TYPE_WITH_CODE(GstGvaMetaConvert, gst_gva_meta_convert, GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gst_gva_meta_convert_debug_category, "gvametaconvert", 0,
                                                "debug category for gvametaconvert element"));

GType gst_gva_metaconvert_get_format(void) {
    static GType gva_metaconvert_format_type = 0;
    static const GEnumValue format_types[] = {
        {GST_GVA_METACONVERT_JSON, "Conversion to GstGVAJSONMeta", FORMAT_JSON_NAME},
        {GST_GVA_METACONVERT_DUMP_DETECTION, "Dump detection to GST debug log", FORMAT_DUMP_DETECTION_NAME},
        {0, NULL, NULL}};

    if (!gva_metaconvert_format_type) {
        gva_metaconvert_format_type = g_enum_register_static("GstGVAMetaconvertFormatType", format_types);
    }
    return gva_metaconvert_format_type;
}

static void gst_gva_metaconvert_set_format(GstGvaMetaConvert *gvametaconvert, GstGVAMetaconvertFormatType format_type) {
    GST_DEBUG_OBJECT(gvametaconvert, "setting format to %d", format_type);

    GHashTable *converters = get_converters();
    convert_function_type convert_function = g_hash_table_lookup(converters, GINT_TO_POINTER(format_type));

    if (convert_function) {
        gvametaconvert->format = format_type;
        gvametaconvert->convert_function = convert_function;
    } else
        g_assert_not_reached();

    g_hash_table_destroy(converters);
}

static void gst_gva_meta_convert_class_init(GstGvaMetaConvertClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    /* Setting up pads and setting metadata should be moved to
       base_class_init if you intend to subclass this class. */
    gst_element_class_add_pad_template(
        GST_ELEMENT_CLASS(klass),
        gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_from_string("ANY")));
    gst_element_class_add_pad_template(
        GST_ELEMENT_CLASS(klass),
        gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string("ANY")));

    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(klass), ELEMENT_LONG_NAME, "Video", ELEMENT_DESCRIPTION,
                                          "Intel Corporation");

    gobject_class->set_property = gst_gva_meta_convert_set_property;
    gobject_class->get_property = gst_gva_meta_convert_get_property;
    gobject_class->dispose = gst_gva_meta_convert_dispose;
    gobject_class->finalize = gst_gva_meta_convert_finalize;
    base_transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_gva_meta_convert_set_caps);
    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_gva_meta_convert_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_gva_meta_convert_stop);
    base_transform_class->sink_event = GST_DEBUG_FUNCPTR(gst_gva_meta_convert_sink_event);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gva_meta_convert_transform_ip);
    element_class->change_state = GST_DEBUG_FUNCPTR(gst_gva_meta_convert_change_state);

    g_object_class_install_property(gobject_class, PROP_FORMAT,
                                    g_param_spec_enum("format", "Format",
                                                      "Output format for conversion. Enum: (1) "
                                                      "json GstGVAJSONMeta representing inference results. For "
                                                      "details on the schema please see the user guide.",
                                                      GST_TYPE_GVA_METACONVERT_FORMAT, DEFAULT_FORMAT,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_ADD_TENSOR_DATA,
                                    g_param_spec_boolean("add-tensor-data", "Add Tensor Data",
                                                         "Add raw tensor data in "
                                                         "addition to detection and classification labels.",
                                                         DEFAULT_ADD_TENSOR_DATA,
                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_SIGNAL_HANDOFFS,
        g_param_spec_boolean("signal-handoffs", "Signal handoffs", "Send signal before pushing the buffer",
                             DEFAULT_SIGNAL_HANDOFFS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_TIMESTAMP_UTC,
                                    g_param_spec_boolean("timestamp-utc", "UTC timestamp",
                                                         "Convert timestamps to UTC format", DEFAULT_TIMESTAMP_UTC,
                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_TIMESTAMP_MICROSECONDS,
        g_param_spec_boolean("timestamp-microseconds", "Microseconds timestamp", "Include microseconds in timestamo",
                             DEFAULT_TIMESTAMP_MICROSECONDS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_SOURCE,
                                    g_param_spec_string("source", "Source URI",
                                                        "User supplied URI identifying the "
                                                        "media source associated with the inference results",
                                                        DEFAULT_SOURCE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_TAGS,
                                    g_param_spec_string("tags", "Custom tags",
                                                        "User supplied JSON object of additional properties added to "
                                                        "each frame's inference results",
                                                        DEFAULT_TAGS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_ADD_EMPTY_DETECTION_RESULTS,
                                    g_param_spec_boolean("add-empty-results", "include metas with no detections",
                                                         "Add metadata when inference is run but no "
                                                         "results meet the detection threshold",
                                                         DEFAULT_ADD_EMPTY_DETECTION_RESULTS,
                                                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(
        gobject_class, PROP_JSON_INDENT,
        g_param_spec_int(
            "json-indent", "JSON indent",
            "To control format of metadata output, indicate the number of spaces to indent blocks of JSON (-1 to 10).",
            MIN_JSON_INDENT, MAX_JSON_INDENT, DEFAULT_JSON_INDENT,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_interpret_signals[SIGNAL_HANDOFF] = g_signal_new(
        "handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(GstGvaMetaConvertClass, handoff), NULL,
        NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1, GST_TYPE_BUFFER | G_SIGNAL_TYPE_STATIC_SCOPE);
}

static void gst_gva_meta_convert_cleanup(GstGvaMetaConvert *gvametaconvert) {
    if (gvametaconvert == NULL)
        return;

    GST_DEBUG_OBJECT(gvametaconvert, "gst_gva_meta_convert_cleanup");

    g_free(gvametaconvert->source);
    gvametaconvert->source = NULL;
    g_free(gvametaconvert->tags);
    gvametaconvert->tags = NULL;
    if (gvametaconvert->info) {
        gst_video_info_free(gvametaconvert->info);
        gvametaconvert->info = NULL;
    }
#ifdef AUDIO
    if (gvametaconvert->audio_info) {
        gst_audio_info_free(gvametaconvert->audio_info);
        gvametaconvert->audio_info = NULL;
    }
#endif
}

static void gst_gva_meta_convert_reset(GstGvaMetaConvert *gvametaconvert) {
    GST_DEBUG_OBJECT(gvametaconvert, "gst_gva_meta_convert_reset");

    if (gvametaconvert == NULL)
        return;

    gst_gva_meta_convert_cleanup(gvametaconvert);

    gvametaconvert->add_tensor_data = DEFAULT_ADD_TENSOR_DATA;
    gvametaconvert->source = g_strdup(DEFAULT_SOURCE);
    gvametaconvert->tags = g_strdup(DEFAULT_TAGS);
    gvametaconvert->add_empty_detection_results = DEFAULT_ADD_EMPTY_DETECTION_RESULTS;
    gvametaconvert->signal_handoffs = DEFAULT_SIGNAL_HANDOFFS;
    gvametaconvert->timestamp_utc = DEFAULT_TIMESTAMP_UTC;
    gvametaconvert->timestamp_microseconds = DEFAULT_TIMESTAMP_MICROSECONDS;
    gst_gva_metaconvert_set_format(gvametaconvert, DEFAULT_FORMAT);
    gvametaconvert->info = NULL;
    gvametaconvert->json_indent = DEFAULT_JSON_INDENT;
#ifdef AUDIO
    gvametaconvert->audio_info = NULL;
#endif
}

static GstStateChangeReturn gst_gva_meta_convert_change_state(GstElement *element, GstStateChange transition) {
    GstStateChangeReturn ret;
    GstGvaMetaConvert *gvametaconvert;

    gvametaconvert = GST_GVA_META_CONVERT(element);
    GST_DEBUG_OBJECT(gvametaconvert, "gst_gva_meta_convert_change_state");

    ret = GST_ELEMENT_CLASS(gst_gva_meta_convert_parent_class)->change_state(element, transition);

    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL: {
        gst_gva_meta_convert_reset(gvametaconvert);
        break;
    }
    default:
        break;
    }
    return ret;
}

static void gst_gva_meta_convert_init(GstGvaMetaConvert *gvametaconvert) {
    gst_gva_meta_convert_reset(gvametaconvert);
}

void gst_gva_meta_convert_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    GstGvaMetaConvert *gvametaconvert = GST_GVA_META_CONVERT(object);

    GST_DEBUG_OBJECT(gvametaconvert, "set_property");

    switch (property_id) {
    case PROP_FORMAT:
        gst_gva_metaconvert_set_format(gvametaconvert, g_value_get_enum(value));
        break;
    case PROP_ADD_TENSOR_DATA:
        gvametaconvert->add_tensor_data = g_value_get_boolean(value);
        break;
    case PROP_SOURCE:
        g_free(gvametaconvert->source);
        gvametaconvert->source = g_value_dup_string(value);
        break;
    case PROP_TAGS:
        g_free(gvametaconvert->tags);
        gvametaconvert->tags = g_value_dup_string(value);
        break;
    case PROP_ADD_EMPTY_DETECTION_RESULTS:
        gvametaconvert->add_empty_detection_results = g_value_get_boolean(value);
        break;
    case PROP_SIGNAL_HANDOFFS:
        gvametaconvert->signal_handoffs = g_value_get_boolean(value);
        break;
    case PROP_TIMESTAMP_UTC:
        gvametaconvert->timestamp_utc = g_value_get_boolean(value);
        break;
    case PROP_TIMESTAMP_MICROSECONDS:
        gvametaconvert->timestamp_microseconds = g_value_get_boolean(value);
        break;
    case PROP_JSON_INDENT:
        gvametaconvert->json_indent = g_value_get_int(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_gva_meta_convert_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GstGvaMetaConvert *gvametaconvert = GST_GVA_META_CONVERT(object);

    GST_DEBUG_OBJECT(gvametaconvert, "get_property");

    switch (property_id) {
    case PROP_FORMAT:
        g_value_set_enum(value, gvametaconvert->format);
        break;
    case PROP_ADD_TENSOR_DATA:
        g_value_set_boolean(value, gvametaconvert->add_tensor_data);
        break;
    case PROP_SOURCE:
        g_value_set_string(value, gvametaconvert->source);
        break;
    case PROP_TAGS:
        g_value_set_string(value, gvametaconvert->tags);
        break;
    case PROP_ADD_EMPTY_DETECTION_RESULTS:
        g_value_set_boolean(value, gvametaconvert->add_empty_detection_results);
        break;
    case PROP_SIGNAL_HANDOFFS:
        g_value_set_boolean(value, gvametaconvert->signal_handoffs);
        break;
    case PROP_TIMESTAMP_UTC:
        g_value_set_boolean(value, gvametaconvert->timestamp_utc);
        break;
    case PROP_TIMESTAMP_MICROSECONDS:
        g_value_set_boolean(value, gvametaconvert->timestamp_microseconds);
        break;
    case PROP_JSON_INDENT:
        g_value_set_int(value, gvametaconvert->json_indent);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_gva_meta_convert_dispose(GObject *object) {
    GstGvaMetaConvert *gvametaconvert = GST_GVA_META_CONVERT(object);

    GST_DEBUG_OBJECT(gvametaconvert, "dispose");

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS(gst_gva_meta_convert_parent_class)->dispose(object);
}

void gst_gva_meta_convert_finalize(GObject *object) {
    GstGvaMetaConvert *gvametaconvert = GST_GVA_META_CONVERT(object);

    GST_DEBUG_OBJECT(gvametaconvert, "finalize");

    /* clean up object here */

    gst_gva_meta_convert_cleanup(gvametaconvert);

    G_OBJECT_CLASS(gst_gva_meta_convert_parent_class)->finalize(object);
}

static gboolean gst_gva_meta_convert_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    UNUSED(outcaps);

    GstGvaMetaConvert *gvametaconvert = GST_GVA_META_CONVERT(trans);
    GST_DEBUG_OBJECT(gvametaconvert, "set_caps");
    GstStructure *caps = gst_caps_get_structure((const GstCaps *)incaps, 0);
    const gchar *name = gst_structure_get_name(caps);
    if (g_strrstr(name, "video")) {

        if (!gvametaconvert->info) {
            gvametaconvert->info = gst_video_info_new();
        }
        gst_video_info_from_caps(gvametaconvert->info, incaps);
    }
#ifdef AUDIO
    else if (g_strrstr(name, "audio")) {
        if (!gvametaconvert->audio_info) {
            gvametaconvert->audio_info = gst_audio_info_new();
        }
        gst_audio_info_from_caps(gvametaconvert->audio_info, incaps);
    }
#endif
    else if (g_strrstr(name, "other")) {
        if (!gvametaconvert->info) {
            gvametaconvert->info = gst_video_info_new();
        }
    } else if (g_strrstr(name, "application/x-radar-processed")) {
        // Accept radar data types - no video/audio info needed
        GST_DEBUG_OBJECT(gvametaconvert, "Accepting radar data type: %s", name);
    } else {
        GST_ERROR_OBJECT(gvametaconvert, "Invalid input caps: %s", name);
        return FALSE;
    }

    return TRUE;
}

/* states */
static gboolean gst_gva_meta_convert_start(GstBaseTransform *trans) {
    GstGvaMetaConvert *gvametaconvert = GST_GVA_META_CONVERT(trans);

    GST_DEBUG_OBJECT(gvametaconvert, "start");

    GST_INFO_OBJECT(gvametaconvert,
                    "%s parameters:\n -- Format: %s\n -- Add tensor data: %s\n -- Source: %s\n -- Tags: %s\n "
                    "-- Add empty detection results: %s\n -- Signal handoffs: %s\n -- UTC timestamps: %s\n --"
                    "Microsecond timestamps: %s\n -- Json indent: %d\n",
                    GST_ELEMENT_NAME(GST_ELEMENT_CAST(gvametaconvert)), format_type_to_string(gvametaconvert->format),
                    gvametaconvert->add_empty_detection_results ? "true" : "false", gvametaconvert->source,
                    gvametaconvert->tags, gvametaconvert->add_empty_detection_results ? "true" : "false",
                    gvametaconvert->signal_handoffs ? "true" : "false",
                    gvametaconvert->timestamp_utc ? "true" : "false",
                    gvametaconvert->timestamp_microseconds ? "true" : "false", gvametaconvert->json_indent);

    return TRUE;
}

static gboolean gst_gva_meta_convert_stop(GstBaseTransform *trans) {
    GstGvaMetaConvert *gvametaconvert = GST_GVA_META_CONVERT(trans);

    GST_DEBUG_OBJECT(gvametaconvert, "stop");

    return TRUE;
}

/* sink and src pad event handlers */
static gboolean gst_gva_meta_convert_sink_event(GstBaseTransform *trans, GstEvent *event) {
    GstGvaMetaConvert *gvametaconvert = GST_GVA_META_CONVERT(trans);

    GST_DEBUG_OBJECT(gvametaconvert, "sink_event");

    return GST_BASE_TRANSFORM_CLASS(gst_gva_meta_convert_parent_class)->sink_event(trans, event);
}

static GstFlowReturn gst_gva_meta_convert_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstGvaMetaConvert *gvametaconvert = GST_GVA_META_CONVERT(trans);

    if (gvametaconvert->signal_handoffs) {
        g_signal_emit(gvametaconvert, gst_interpret_signals[SIGNAL_HANDOFF], 0, buf);
    } else if (gvametaconvert->convert_function) {
        gvametaconvert->convert_function(gvametaconvert, buf);
    }

    return GST_FLOW_OK;
}

static gboolean plugin_init(GstPlugin *plugin) {
    if (!gst_element_register(plugin, "gvametaconvert", GST_RANK_NONE, GST_TYPE_GVA_META_CONVERT))
        return FALSE;

    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, gvametaconvert, PRODUCT_FULL_NAME " gvametaconvert element",
                  plugin_init, PLUGIN_VERSION, PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
