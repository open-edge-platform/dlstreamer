/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Bin wrapper around the hidden GstGvaWatermark3DRender filter. It accepts VA/DMABuf/system
// memory (like the 2D gvawatermark) and internally converts to system-memory BGR, which the
// OpenCV-based renderer requires. The renderer is instantiated via g_object_new, so it is not
// exposed as a public element.

#include "gvawatermark3d.h"
#include "gvawatermark3drender.h"

#include "gva_caps.h"

#include <gst/gst.h>

#define ELEMENT_LONG_NAME "Bin element for monocular 3D detection results labeling"
#define ELEMENT_DESCRIPTION "Overlays 3D bounding boxes (GstAnalyticsCamera3DODMtd) on the video frame."

GST_DEBUG_CATEGORY_STATIC(gst_gvawatermark3d_debug_category);
#define GST_CAT_DEFAULT gst_gvawatermark3d_debug_category

enum {
    PROP_0,
    PROP_INTRINSICS_FILE,
    PROP_CALIBRATION_FILE,
};

G_DEFINE_TYPE_WITH_CODE(GstGvaWatermark3D, gst_gvawatermark3d, GST_TYPE_BIN,
                        GST_DEBUG_CATEGORY_INIT(gst_gvawatermark3d_debug_category, "gvawatermark3d", 0,
                                                "debug category for gvawatermark3d bin"));

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVA_CAPS));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVA_CAPS));

static void gst_gvawatermark3d_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaWatermark3D *self = GST_GVAWATERMARK3D(object);
    switch (prop_id) {
    case PROP_INTRINSICS_FILE:
        g_object_set_property(G_OBJECT(self->render), "intrinsics-file", value);
        break;
    case PROP_CALIBRATION_FILE:
        g_object_set_property(G_OBJECT(self->render), "calibration-file", value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gvawatermark3d_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaWatermark3D *self = GST_GVAWATERMARK3D(object);
    switch (prop_id) {
    case PROP_INTRINSICS_FILE:
        g_object_get_property(G_OBJECT(self->render), "intrinsics-file", value);
        break;
    case PROP_CALIBRATION_FILE:
        g_object_get_property(G_OBJECT(self->render), "calibration-file", value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gvawatermark3d_class_init(GstGvaWatermark3DClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gobject_class->set_property = gst_gvawatermark3d_set_property;
    gobject_class->get_property = gst_gvawatermark3d_get_property;

    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    gst_element_class_set_static_metadata(element_class, ELEMENT_LONG_NAME, "Video", ELEMENT_DESCRIPTION,
                                          "Intel Corporation");

    g_object_class_install_property(gobject_class, PROP_INTRINSICS_FILE,
                                    g_param_spec_string("intrinsics-file", "Intrinsics File",
                                                        "Path to JSON file with camera intrinsics", NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_CALIBRATION_FILE,
        g_param_spec_string("calibration-file", "Calibration File",
                            "Camera calibration for monocular 3D boxes: KITTI .txt (P2 row) or JSON "
                            "(intrinsic_matrix 3x3 / projection_matrix 3x4).",
                            NULL, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_gvawatermark3d_init(GstGvaWatermark3D *self) {
    self->render = GST_ELEMENT(g_object_new(GST_TYPE_GVA_WATERMARK3D_RENDER, NULL));
    self->videoconvert = gst_element_factory_make("videoconvert", NULL);
    self->capsfilter = gst_element_factory_make("capsfilter", NULL);
    self->videoconvert_out = gst_element_factory_make("videoconvert", NULL);
    // Optional VA download so VAMemory/DMABuf/VASurface input reaches the CPU renderer as system memory.
    self->vaconvert = gst_element_factory_make("vapostproc", NULL);

    if (!self->render || !self->videoconvert || !self->capsfilter || !self->videoconvert_out) {
        GST_ERROR_OBJECT(self, "Failed to create internal gvawatermark3d elements");
        return;
    }

    GstCaps *bgr = gst_caps_from_string("video/x-raw, format=(string)BGR");
    g_object_set(self->capsfilter, "caps", bgr, NULL);
    gst_caps_unref(bgr);

    if (self->vaconvert)
        gst_bin_add(GST_BIN(self), self->vaconvert);
    gst_bin_add_many(GST_BIN(self), self->videoconvert, self->capsfilter, self->render, self->videoconvert_out, NULL);

    // Chain: [vapostproc ->] videoconvert -> capsfilter(BGR) -> render -> videoconvert(out)
    GstElement *head = self->videoconvert;
    if (self->vaconvert) {
        if (!gst_element_link(self->vaconvert, self->videoconvert))
            GST_ERROR_OBJECT(self, "Failed to link vapostproc -> videoconvert");
        head = self->vaconvert;
    }
    if (!gst_element_link_many(self->videoconvert, self->capsfilter, self->render, self->videoconvert_out, NULL))
        GST_ERROR_OBJECT(self, "Failed to link internal gvawatermark3d chain");

    GstPad *sink_target = gst_element_get_static_pad(head, "sink");
    self->sinkpad = gst_ghost_pad_new("sink", sink_target);
    gst_object_unref(sink_target);
    gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);

    GstPad *src_target = gst_element_get_static_pad(self->videoconvert_out, "src");
    self->srcpad = gst_ghost_pad_new("src", src_target);
    gst_object_unref(src_target);
    gst_element_add_pad(GST_ELEMENT(self), self->srcpad);
}
