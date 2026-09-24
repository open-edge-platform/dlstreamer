/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvamono3d.h"

#include "config.h"
#include "gva_caps.h"

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#define ELEMENT_LONG_NAME "Monocular 3D object detection (generates GstAnalyticsCamera3DODMtd)"
#define ELEMENT_DESCRIPTION                                                                                            \
    "Performs monocular 3D object detection (e.g. MonoDETR). Consumes an image plus camera "                           \
    "calibration and emits 2D detections annotated with 3D cuboids (location, dimensions, orientation)."

enum {
    PROP_0,
    PROP_THRESHOLD,
    PROP_CALIBRATION_FILE,
};

#define DEFAULT_MIN_THRESHOLD 0.
#define DEFAULT_MAX_THRESHOLD 1.
#define DEFAULT_THRESHOLD 0.5

GST_DEBUG_CATEGORY_STATIC(gst_gva_mono3d_debug_category);
#define GST_CAT_DEFAULT gst_gva_mono3d_debug_category

G_DEFINE_TYPE_WITH_CODE(GstGvaMono3d, gst_gva_mono3d, GST_TYPE_GVA_BASE_INFERENCE,
                        GST_DEBUG_CATEGORY_INIT(gst_gva_mono3d_debug_category, "gvamono3d", 0,
                                                "debug category for gvamono3d element"));

static gboolean gst_gva_mono3d_start(GstBaseTransform *trans) {
    GstGvaMono3d *gvamono3d = GST_GVA_MONO3D(trans);

    GST_INFO_OBJECT(gvamono3d, "%s parameters:\n -- Threshold: %f\n -- Calibration file: %s\n",
                    GST_ELEMENT_NAME(GST_ELEMENT_CAST(gvamono3d)), gvamono3d->threshold,
                    gvamono3d->calibration_file ? gvamono3d->calibration_file : "(none)");

    return GST_BASE_TRANSFORM_CLASS(gst_gva_mono3d_parent_class)->start(trans);
}

static void gst_gva_mono3d_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    GstGvaMono3d *gvamono3d = GST_GVA_MONO3D(object);

    switch (property_id) {
    case PROP_THRESHOLD:
        gvamono3d->threshold = g_value_get_float(value);
        gvamono3d->threshold_explicitly_set = TRUE;
        break;
    case PROP_CALIBRATION_FILE:
        g_free(gvamono3d->calibration_file);
        gvamono3d->calibration_file = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void gst_gva_mono3d_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GstGvaMono3d *gvamono3d = GST_GVA_MONO3D(object);

    switch (property_id) {
    case PROP_THRESHOLD:
        g_value_set_float(value, gvamono3d->threshold);
        break;
    case PROP_CALIBRATION_FILE:
        g_value_set_string(value, gvamono3d->calibration_file);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void gst_gva_mono3d_finalize(GObject *object) {
    GstGvaMono3d *gvamono3d = GST_GVA_MONO3D(object);
    g_free(gvamono3d->calibration_file);
    gvamono3d->calibration_file = NULL;
    G_OBJECT_CLASS(gst_gva_mono3d_parent_class)->finalize(object);
}

void gst_gva_mono3d_class_init(GstGvaMono3dClass *klass) {
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    base_transform_class->start = gst_gva_mono3d_start;

    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_pad_template(
        element_class, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_from_string(GVA_CAPS)));
    gst_element_class_add_pad_template(
        element_class, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, gst_caps_from_string(GVA_CAPS)));

    gst_element_class_set_static_metadata(element_class, ELEMENT_LONG_NAME, "Video", ELEMENT_DESCRIPTION,
                                          "Intel Corporation");

    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->set_property = gst_gva_mono3d_set_property;
    gobject_class->get_property = gst_gva_mono3d_get_property;
    gobject_class->finalize = gst_gva_mono3d_finalize;

    g_object_class_install_property(
        gobject_class, PROP_THRESHOLD,
        g_param_spec_float("threshold", "Threshold",
                           "Only detections with confidence above the threshold are attached to the frame",
                           DEFAULT_MIN_THRESHOLD, DEFAULT_MAX_THRESHOLD, DEFAULT_THRESHOLD,
                           (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CALIBRATION_FILE,
        g_param_spec_string("calibration-file", "Calibration File",
                            "Path to camera calibration: KITTI .txt (P2 row) or JSON with "
                            "\"intrinsic_matrix\" (3x3) / \"projection_matrix\" (3x4) and optional \"image_size\".",
                            NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

void gst_gva_mono3d_init(GstGvaMono3d *gvamono3d) {
    GST_DEBUG_OBJECT(gvamono3d, "gst_gva_mono3d_init");

    gvamono3d->base_inference.type = GST_GVA_MONO3D_TYPE;
    gvamono3d->threshold = DEFAULT_THRESHOLD;
    gvamono3d->threshold_explicitly_set = FALSE;
    gvamono3d->calibration_file = NULL;
}
