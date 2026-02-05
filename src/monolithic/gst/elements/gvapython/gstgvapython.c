/*******************************************************************************
 * Copyright (C) 2020-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <Python.h>
#include <gst/gst.h>

#include "gstgvapython.h"
#include "gva_caps.h"
#include "python_callback_c.h"
#include "utils.h"

#define ELEMENT_LONG_NAME "Python callback provider"
#define ELEMENT_DESCRIPTION                                                                                            \
    "Provides a callback to execute user-defined Python functions on every frame. "                                    \
    "Can be used for metadata conversion, inference post-processing, and other tasks."

GST_DEBUG_CATEGORY(gst_gva_python_debug_category);
#define GST_CAT_DEFAULT gst_gva_python_debug_category

enum { PROP_0, PROP_MODULE, PROP_CLASS, PROP_FUNCTION, PROP_ARGUMENT, PROP_KW_ARGUMENT };

#define DEFAULT_MODULE ""
#define DEFAULT_CLASS ""
#define DEFAULT_FUNCTION "process_frame"
#define DEFAULT_ARGUMENT "[]"
#define DEFAULT_KW_ARGUMENT "{}"

#ifdef NDEBUG
#define LOG_PYTHON_ERROR(ELEMENT, ...) GST_ERROR_OBJECT(ELEMENT, __VA_ARGS__)
#else
#define LOG_PYTHON_ERROR(ELEMENT, ...)                                                                                 \
    GST_ERROR_OBJECT(ELEMENT, __VA_ARGS__);                                                                            \
    PyErr_Print()
#endif

/* prototypes */
static void gst_gva_python_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_gva_python_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static gboolean gst_gva_python_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);
static gboolean gst_gva_python_start(GstBaseTransform *trans);
static void gst_gva_python_dispose(GObject *object);
static void gst_gva_python_finalize(GObject *object);

static GstFlowReturn gst_gva_python_transform_ip(GstBaseTransform *trans, GstBuffer *buf);

/* class initialization */
static void gst_gva_python_init(GstGvaPython *gva_python);

G_DEFINE_TYPE_WITH_CODE(GstGvaPython, gst_gva_python, GST_TYPE_BASE_TRANSFORM,
                        GST_DEBUG_CATEGORY_INIT(gst_gva_python_debug_category, "gvapython", 0,
                                                "debug category for gvapython element"));

gboolean gst_gva_python_propose_allocation(GstBaseTransform *trans, GstQuery *decide_query, GstQuery *query) {
    UNUSED(decide_query);
    UNUSED(trans);
    if (query) {
        gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
        return TRUE;
    }
    return FALSE;
}

static void gst_gva_python_class_init(GstGvaPythonClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_pad_template(
        element_class, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_from_string("ANY")));
    gst_element_class_add_pad_template(
        element_class, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string("ANY")));

    gst_element_class_set_static_metadata(element_class, ELEMENT_LONG_NAME, "Video", ELEMENT_DESCRIPTION,
                                          "Intel Corporation");
    gobject_class->set_property = gst_gva_python_set_property;
    gobject_class->get_property = gst_gva_python_get_property;
    gobject_class->dispose = gst_gva_python_dispose;
    gobject_class->finalize = gst_gva_python_finalize;
    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_gva_python_start);
    base_transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_gva_python_set_caps);
    base_transform_class->transform = NULL;
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gva_python_transform_ip);
    base_transform_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_gva_python_propose_allocation);

    g_object_class_install_property(gobject_class, PROP_MODULE,
                                    g_param_spec_string("module", "Python module name", "Python module name",
                                                        DEFAULT_MODULE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(gobject_class, PROP_CLASS,
                                    g_param_spec_string("class", "(optional) Python class name", "Python class name",
                                                        DEFAULT_CLASS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(
        gobject_class, PROP_ARGUMENT,
        g_param_spec_string("arg",
                            "(optional) Argument for Python class initialization."
                            "Argument is interpreted as a JSON value or JSON array."
                            "If passed multiple times arguments are combined into a single JSON array.",
                            "Argument for Python class initialization."
                            "Argument is interpreted as a JSON value or JSON array."
                            "If passed multiple times arguments are combined into a single JSON array.",
                            DEFAULT_ARGUMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(
        gobject_class, PROP_KW_ARGUMENT,
        g_param_spec_string("kwarg",
                            "(optional) Keyword argument for Python class initialization"
                            "Keyword argument is interpreted as a JSON object."
                            "If passed multiple times keyword arguments are combined into a single JSON object.",
                            "Keyword argument for Python class initialization."
                            "Keyword argument is interpreted as a JSON object."
                            "If passed multiple times keyword arguments are combined into a single JSON object.",
                            DEFAULT_KW_ARGUMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(gobject_class, PROP_FUNCTION,
                                    g_param_spec_string("function", "Python function name", "Python function name",
                                                        DEFAULT_FUNCTION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void gst_gva_python_init(GstGvaPython *gvapython) {
    GST_DEBUG_OBJECT(gvapython, "gva_python_init");
    gvapython->module_name = NULL;
    gvapython->class_name = NULL;
    create_arguments(&gvapython->args, &gvapython->kwargs);
    gvapython->function_name = g_strdup(DEFAULT_FUNCTION);
    gvapython->python_callback = NULL;
}

void gst_gva_python_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GstGvaPython *gvapython = GST_GVA_PYTHON(object);
    gchar *argument_string = NULL;
    GST_DEBUG_OBJECT(gvapython, "get_property");
    switch (property_id) {
    case PROP_MODULE:
        g_value_set_string(value, gvapython->module_name);
        break;
    case PROP_CLASS:
        g_value_set_string(value, gvapython->class_name);
        break;
    case PROP_FUNCTION:
        g_value_set_string(value, gvapython->function_name);
        break;
    case PROP_ARGUMENT:
        argument_string = get_arguments_string(gvapython->args);
        g_value_set_string(value, argument_string);
        g_free(argument_string);
        break;
    case PROP_KW_ARGUMENT:
        argument_string = get_arguments_string(gvapython->kwargs);
        g_value_set_string(value, argument_string);
        g_free(argument_string);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_gva_python_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    GstGvaPython *gvapython = GST_GVA_PYTHON(object);
    GST_DEBUG_OBJECT(gvapython, "set_property");

    switch (property_id) {
    case PROP_MODULE:
        g_free(gvapython->module_name);
        gvapython->module_name = g_value_dup_string(value);
        break;
    case PROP_CLASS:
        g_free(gvapython->class_name);
        gvapython->class_name = g_value_dup_string(value);
        break;
    case PROP_FUNCTION:
        g_free(gvapython->function_name);
        gvapython->function_name = g_value_dup_string(value);
        break;
    case PROP_ARGUMENT:
        if (!update_arguments(g_value_get_string(value), &gvapython->args)) {
            GST_ELEMENT_ERROR(gvapython, LIBRARY, INIT, ("Error updating arguments"),
                              ("%s is invalid JSON", g_value_get_string(value)));
        }
        break;
    case PROP_KW_ARGUMENT:
        if (!update_keyword_arguments(g_value_get_string(value), &gvapython->kwargs)) {
            GST_ELEMENT_ERROR(gvapython, LIBRARY, INIT, ("Error updating arguments"),
                              ("%s is invalid JSON", g_value_get_string(value)));
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static gboolean gst_gva_python_start(GstBaseTransform *trans) {
    GstGvaPython *gvapython = GST_GVA_PYTHON(trans);
    GST_DEBUG_OBJECT(gvapython, "start");
    if (gvapython->python_callback) {
        return TRUE;
    }
    gchar *argument_string = NULL;
    gchar *keyword_argument_string = NULL;
    if (!gvapython->module_name) {
        GST_ERROR_OBJECT(gvapython, "Parameter 'module' not set");
        GST_ELEMENT_ERROR(gvapython, LIBRARY, INIT, ("Error creating Python callback"), ("Invalid module"));
        return FALSE;
    }
    if (!gvapython->function_name) {
        GST_ERROR_OBJECT(gvapython, "Parameter 'function-name' is null");
        GST_ELEMENT_ERROR(gvapython, LIBRARY, INIT, ("Error creating Python callback."), ("Invalid function name."));
        return FALSE;
    }
    argument_string = get_arguments_string(gvapython->args);
    keyword_argument_string = get_arguments_string(gvapython->kwargs);

    GST_INFO_OBJECT(gvapython,
                    "%s parameters:\n -- Module: %s\n -- Class: %s\n -- Function: %s\n -- Arg: %s\n "
                    "-- Keyword Arg: %s\n",
                    GST_ELEMENT_NAME(GST_ELEMENT_CAST(gvapython)), gvapython->module_name, gvapython->class_name,
                    gvapython->function_name, argument_string, keyword_argument_string);

    if (keyword_argument_string && argument_string) {
        gvapython->python_callback =
            create_python_callback(gvapython->module_name, gvapython->class_name, gvapython->function_name,
                                   argument_string, keyword_argument_string);
    }

    if (!gvapython->python_callback) {
        GST_ELEMENT_ERROR(trans, LIBRARY, INIT, ("Error creating Python callback"),
                          ("Module: %s\n Class: %s\n Function: %s\n Arg: %s\n Keyword Arg: %s\n",
                           gvapython->module_name, gvapython->class_name, gvapython->function_name, argument_string,
                           keyword_argument_string));
    }

    g_free(argument_string);
    g_free(keyword_argument_string);
    return gvapython->python_callback != NULL;
}

static gboolean gst_gva_python_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    UNUSED(outcaps);
    GstGvaPython *gvapython = GST_GVA_PYTHON(trans);
    GST_DEBUG_OBJECT(gvapython, "set_caps");
    return set_python_callback_caps(gvapython->python_callback, incaps);
}

void gst_gva_python_dispose(GObject *object) {
    GstGvaPython *gvapython = GST_GVA_PYTHON(object);

    GST_DEBUG_OBJECT(gvapython, "dispose");

    /* clean up as possible.  may be called multiple times */

    G_OBJECT_CLASS(gst_gva_python_parent_class)->dispose(object);
}

void gst_gva_python_finalize(GObject *object) {
    GstGvaPython *gvapython = GST_GVA_PYTHON(object);

    GST_DEBUG_OBJECT(gvapython, "finalize");

    g_free(gvapython->module_name);
    gvapython->module_name = NULL;
    g_free(gvapython->class_name);
    gvapython->class_name = NULL;
    g_free(gvapython->function_name);
    gvapython->function_name = NULL;

    if (gvapython->python_callback) {
        delete_python_callback(gvapython->python_callback);
        gvapython->python_callback = NULL;
    }
    if (gvapython->args) {
        delete_arguments(gvapython->args);
        gvapython->args = NULL;
    }

    if (gvapython->kwargs) {
        delete_arguments(gvapython->kwargs);
        gvapython->kwargs = NULL;
    }
    G_OBJECT_CLASS(gst_gva_python_parent_class)->finalize(object);
}

static GstFlowReturn gst_gva_python_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstGvaPython *gvapython = GST_GVA_PYTHON(trans);
    GST_DEBUG_OBJECT(gvapython, "transform_ip");

    return invoke_python_callback(gvapython, buf);
}

static gboolean plugin_init(GstPlugin *plugin) {
    if (!gst_element_register(plugin, "gvapython", GST_RANK_NONE, GST_TYPE_GVA_PYTHON)) {
        return FALSE;
    }
    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, gvapython, ELEMENT_DESCRIPTION, plugin_init, PLUGIN_VERSION,
                  PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
