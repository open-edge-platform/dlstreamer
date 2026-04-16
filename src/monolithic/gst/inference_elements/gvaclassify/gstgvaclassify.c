/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvaclassify.h"

#include "classification_history.h"
#include "pre_processors.h"

#include "config.h"
#include "utils.h"

#include "gva_caps.h"

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#define ELEMENT_LONG_NAME "Object classification (requires GstVideoRegionOfInterestMeta on input)"
#define ELEMENT_DESCRIPTION                                                                                            \
    "Performs object classification. Accepts the ROI or full frame as an input and "                                   \
    "outputs classification results with metadata."

enum {
    PROP_0,
    PROP_RECLASSIFY_INTERVAL,
    PROP_SKIP_RAW_TENSORS,
};

#define DEFAULT_RECLASSIFY_INTERVAL 1
#define DEFAULT_MIN_RECLASSIFY_INTERVAL 0
#define DEFAULT_MAX_RECLASSIFY_INTERVAL UINT_MAX
#define DEFAULT_SKIP_RAW_TENSORS FALSE

GST_DEBUG_CATEGORY_STATIC(gst_gva_classify_debug_category);
#define GST_CAT_DEFAULT gst_gva_classify_debug_category

G_DEFINE_TYPE_WITH_CODE(GstGvaClassify, gst_gva_classify, GST_TYPE_GVA_BASE_INFERENCE,
                        GST_DEBUG_CATEGORY_INIT(gst_gva_classify_debug_category, "gvaclassify", 0,
                                                "debug category for gvaclassify element"));

static GstPadProbeReturn FillROIParamsCallback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);
static void gst_gva_classify_finalize(GObject *);
static void gst_gva_classify_cleanup(GstGvaClassify *);
static gboolean gst_gva_classify_check_properties_correctness(GstGvaClassify *gvaclassify);
static gboolean gst_gva_classify_start(GstBaseTransform *trans);

void gst_gva_classify_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    GstGvaClassify *gvaclassify = GST_GVA_CLASSIFY(object);

    GST_DEBUG_OBJECT(gvaclassify, "set_property");

    static gulong probe_id = 0;

    switch (property_id) {
    case PROP_RECLASSIFY_INTERVAL: {
        guint newValue = g_value_get_uint(value);
        guint oldValue = gvaclassify->reclassify_interval;
        if (newValue != oldValue) {
            if (oldValue == DEFAULT_RECLASSIFY_INTERVAL) {
                probe_id =
                    gst_pad_add_probe(gvaclassify->base_inference.base_transform.srcpad, GST_PAD_PROBE_TYPE_BUFFER,
                                      FillROIParamsCallback, gvaclassify->classification_history, NULL);
            } else if (newValue == DEFAULT_RECLASSIFY_INTERVAL) { // not tested
                gst_pad_remove_probe(gvaclassify->base_inference.base_transform.srcpad, probe_id);
                probe_id = 0;
            }
            gvaclassify->reclassify_interval = newValue;
        }
        break;
    }
    case PROP_SKIP_RAW_TENSORS:
        gvaclassify->skip_raw_tensors = g_value_get_boolean(value);
        break;
    default: {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
    }
}

void gst_gva_classify_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GstGvaClassify *gvaclassify = GST_GVA_CLASSIFY(object);

    GST_DEBUG_OBJECT(gvaclassify, "get_property");

    switch (property_id) {
    case PROP_RECLASSIFY_INTERVAL:
        g_value_set_uint(value, gvaclassify->reclassify_interval);
        break;
    case PROP_SKIP_RAW_TENSORS:
        g_value_set_boolean(value, gvaclassify->skip_raw_tensors);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_gva_classify_class_init(GstGvaClassifyClass *gvaclassify_class) {
    GstElementClass *element_class = GST_ELEMENT_CLASS(gvaclassify_class);

    gst_element_class_add_pad_template(
        element_class, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_from_string(GVA_CAPS)));
    gst_element_class_add_pad_template(
        element_class, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string(GVA_CAPS)));

    gst_element_class_set_static_metadata(element_class, ELEMENT_LONG_NAME, "Video", ELEMENT_DESCRIPTION,
                                          "Intel Corporation");

    GObjectClass *gobject_class = G_OBJECT_CLASS(gvaclassify_class);
    gobject_class->set_property = gst_gva_classify_set_property;
    gobject_class->get_property = gst_gva_classify_get_property;
    gobject_class->finalize = gst_gva_classify_finalize;

    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(gvaclassify_class);
    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_gva_classify_start);

    g_object_class_install_property(
        gobject_class, PROP_RECLASSIFY_INTERVAL,
        g_param_spec_uint(
            "reclassify-interval", "Reclassify Interval",
            "Determines how often to reclassify tracked objects. Only valid when used in conjunction with gvatrack.\n"
            "The following values are acceptable:\n"
            "- 0 - Do not reclassify tracked objects\n"
            "- 1 - Always reclassify tracked objects\n"
            "- 2:N - Tracked objects will be reclassified every N frames. Note the inference-interval is applied "
            "before "
            "determining if an object is to be reclassified (i.e. classification only occurs at a multiple of the "
            "inference interval)",
            DEFAULT_MIN_RECLASSIFY_INTERVAL, DEFAULT_MAX_RECLASSIFY_INTERVAL, DEFAULT_RECLASSIFY_INTERVAL,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SKIP_RAW_TENSORS,
        g_param_spec_boolean(
            "skip-raw-tensors", "Skip Raw Tensors",
            "Skips attaching raw output tensors to metadata for gvaclassify to_tensor post-processing.",
            DEFAULT_SKIP_RAW_TENSORS, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

void gst_gva_classify_init(GstGvaClassify *gvaclassify) {
    GST_DEBUG_OBJECT(gvaclassify, "gst_gva_classify_init");
    GST_DEBUG_OBJECT(gvaclassify, "%s", GST_ELEMENT_NAME(GST_ELEMENT(gvaclassify)));

    if (gvaclassify == NULL)
        return;
    gst_gva_classify_cleanup(gvaclassify);

    gvaclassify->base_inference.type = GST_GVA_CLASSIFY_TYPE;
    gvaclassify->base_inference.inference_region = ROI_LIST;
    gvaclassify->reclassify_interval = DEFAULT_RECLASSIFY_INTERVAL;
    gvaclassify->skip_raw_tensors = DEFAULT_SKIP_RAW_TENSORS;
    gvaclassify->classification_history = create_classification_history(gvaclassify);
    if (gvaclassify->classification_history == NULL)
        return;

    gvaclassify->base_inference.specific_roi_filter = IS_ROI_CLASSIFICATION_NEEDED;
}

void gst_gva_classify_cleanup(GstGvaClassify *gvaclassify) {
    if (gvaclassify == NULL)
        return;

    GST_DEBUG_OBJECT(gvaclassify, "gva_classify_cleanup");

    if (gvaclassify->classification_history) {
        release_classification_history(gvaclassify->classification_history);
        gvaclassify->classification_history = NULL;
    }
}

void gst_gva_classify_finalize(GObject *object) {
    GstGvaClassify *gvaclassify = GST_GVA_CLASSIFY(object);

    GST_DEBUG_OBJECT(gvaclassify, "finalize");

    gst_gva_classify_cleanup(gvaclassify);

    G_OBJECT_CLASS(gst_gva_classify_parent_class)->finalize(object);
}

GstPadProbeReturn FillROIParamsCallback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    UNUSED(pad);
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buffer != NULL && user_data != NULL)
        fill_roi_params_from_history((struct ClassificationHistory *)user_data, buffer);

    return GST_PAD_PROBE_OK;
}

gboolean gst_gva_classify_check_properties_correctness(GstGvaClassify *gvaclassify) {
    GvaBaseInference *base_inference = GVA_BASE_INFERENCE(gvaclassify);

    if (base_inference->inference_region == FULL_FRAME && gvaclassify->reclassify_interval != 1) {
        GST_ERROR_OBJECT(gvaclassify,
                         ("You cannot use 'reclassify-interval' property on gvaclassify if you set 'full-frame' for "
                          "'inference-region' property."));
        return FALSE;
    }

    return TRUE;
}

gboolean gst_gva_classify_start(GstBaseTransform *trans) {
    GstGvaClassify *gvaclassify = GST_GVA_CLASSIFY(trans);

    GST_INFO_OBJECT(gvaclassify, "%s parameters:\n -- Reclassify interval: %d\n -- Skip raw tensors: %s\n",
                    GST_ELEMENT_NAME(GST_ELEMENT_CAST(gvaclassify)), gvaclassify->reclassify_interval,
                    gvaclassify->skip_raw_tensors ? "true" : "false");

    if (!gst_gva_classify_check_properties_correctness(gvaclassify))
        return FALSE;

    return GST_BASE_TRANSFORM_CLASS(gst_gva_classify_parent_class)->start(trans);
}

static gboolean plugin_init(GstPlugin *plugin) {
    if (!gst_element_register(plugin, "gvaclassify", GST_RANK_NONE, GST_TYPE_GVA_CLASSIFY))
        return FALSE;

    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, gvaclassify, PRODUCT_FULL_NAME " gvaclassify element",
                  plugin_init, PLUGIN_VERSION, PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
