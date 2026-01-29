/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gvawatermark.h"
#include "gstgvawatermarkimpl.h"
#include "gvawatermarkcaps.h"

#include "scope_guard.h"

#include <gst/video/video.h>

#include <string>

#define ELEMENT_LONG_NAME "Bin element for detection/classification/recognition results labeling"
#define ELEMENT_DESCRIPTION "Overlays the metadata on the video frame to visualize the inference results."

GST_DEBUG_CATEGORY_STATIC(gst_gva_watermark_debug_category);
#define GST_CAT_DEFAULT gst_gva_watermark_debug_category

#define DEFAULT_DEVICE "CPU"

G_DEFINE_TYPE_WITH_CODE(GstGvaWatermark, gst_gva_watermark, GST_TYPE_BIN,
                        GST_DEBUG_CATEGORY_INIT(gst_gva_watermark_debug_category, "gvawatermark", 0,
                                                "debug category for gvawatermark element"));

static GstStaticPadTemplate sinktemplate =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVA_CAPS));

static GstStaticPadTemplate srctemplate =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVA_CAPS));

static void gst_gva_watermark_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static void gst_gva_watermark_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gst_gva_watermark_finalize(GObject *object);
static GstStateChangeReturn gva_watermark_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_gva_watermark_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);

namespace {
static const gchar *get_caps_str_with_feature(CapsFeature mem_type) {
    if (mem_type == VA_SURFACE_CAPS_FEATURE)
        return "video/x-raw(" VASURFACE_FEATURE_STR "), format=" WATERMARK_PREFERRED_REMOTE_FORMAT;
    if (mem_type == VA_MEMORY_CAPS_FEATURE)
        return "video/x-raw(" VAMEMORY_FEATURE_STR "), format=" WATERMARK_PREFERRED_REMOTE_FORMAT;
    if (mem_type == DMA_BUF_CAPS_FEATURE)
        return "video/x-raw(" DMABUF_FEATURE_STR "), format=" WATERMARK_VA_PREFERRED_REMOTE_FORMAT;
    if (mem_type == SYSTEM_MEMORY_CAPS_FEATURE)
        return "video/x-raw";
    g_assert(FALSE && "Only VASurface, VAMemory, DMABuf, and System memory are supported !");
    return "";
}

static bool is_caps_format_equal(GstCaps *caps, const std::string &format) {
    bool result = true;

    auto size = gst_caps_get_size(caps);
    for (size_t i = 0; i < size; ++i) {
        auto structure = gst_caps_get_structure(caps, i);
        auto str = gst_structure_get_string(structure, "format");
        if (str == nullptr || str != format) {
            return false;
        }
    }

    return result;
}

} /* anonymous namespace */

static void gst_gva_watermark_class_init(GstGvaWatermarkClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &sinktemplate);
    gst_element_class_add_static_pad_template(element_class, &srctemplate);

    gst_element_class_set_static_metadata(element_class, ELEMENT_LONG_NAME, "Video", ELEMENT_DESCRIPTION,
                                          "Intel® Corporation");

    element_class->change_state = gva_watermark_change_state;

    gobject_class->set_property = gst_gva_watermark_set_property;
    gobject_class->get_property = gst_gva_watermark_get_property;
    gobject_class->finalize = gst_gva_watermark_finalize;

    const auto kDefaultGParamFlags =
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

    g_object_class_install_property(gobject_class, PROP_DEVICE,
                                    g_param_spec_string("device", "Target device", "CPU or GPU. Default is CPU.",
                                                        DEFAULT_DEVICE, kDefaultGParamFlags));

    g_object_class_install_property(gobject_class, PROP_OBB,
                                    g_param_spec_boolean("obb", "Oriented Bounding Box",
                                                         "If true, draw oriented bounding box instead of object mask",
                                                         false, kDefaultGParamFlags));
    g_object_class_install_property(
        gobject_class, PROP_DISPL_AVGFPS,
        g_param_spec_boolean(
            "displ-avgfps", "Display Average FPS",
            "If true, display the average FPS read from gvafpscounter element on the output video, (default false)\n"
            "\t\t\tThe gvafpscounter element must be present in the pipeline.\n"
            "\t\t\te.g.: ... ! gvawatermark displ-avgfps=true ! gvafpscounter ! ...",
            false, kDefaultGParamFlags));

    g_object_class_install_property(
        gobject_class, PROP_DISPL_CFG,
        g_param_spec_string(
            "displ-cfg", "Gvawatermark display configuration",
            "Comma separated list of KEY=VALUE parameters of displayed notations.\n"
            "\t\t\tAvailable options: \n"
            "\t\t\tshow-labels=true|false - enable/disable display of text labels (default true)\n"
            "\t\t\ttext-scale=<0.1-2.0> - scale factor for text labels (default 1.0)\n"
            "\t\t\tthickness=<uint> - thickness of bounding box (default 2)\n"
            "\t\t\tcolor-idx=<int> - color index for bounding box, keypoints, text (default -1 - default colors)\n"
            "\t\t\t (0 - red, 1 - green, 2 - blue)\n"
            "\t\t\te.g.: displ-cfg=show-labels=off\n"
            "\t\t\te.g.: displ-cfg=text-scale=0.5,thickness=3,color-idx=0",
            nullptr, kDefaultGParamFlags));
}

static void gst_gva_watermark_init(GstGvaWatermark *self) {
    GstPadTemplate *pad_tmpl = gst_static_pad_template_get(&sinktemplate);
    self->sinkpad = gst_ghost_pad_new_no_target_from_template("sink", pad_tmpl);
    gst_object_unref(pad_tmpl);

    pad_tmpl = gst_static_pad_template_get(&srctemplate);
    self->srcpad = gst_ghost_pad_new_no_target_from_template("src", pad_tmpl);
    gst_object_unref(pad_tmpl);

    gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);
    gst_element_add_pad(GST_ELEMENT(self), self->srcpad);

    gst_pad_set_event_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_gva_watermark_sink_event));

    self->identity = gst_element_factory_make("identity", nullptr);
    if (!self->identity)
        GST_ERROR_OBJECT(self, "Could not create identity instance");

    self->watermarkimpl = gst_element_factory_make("gvawatermarkimpl", nullptr);
    if (!self->watermarkimpl)
        GST_ERROR_OBJECT(self, "Could not create gvawatermark instance");

    gst_bin_add_many(GST_BIN(self), GST_ELEMENT(gst_object_ref(self->identity)),
                     GST_ELEMENT(gst_object_ref(self->watermarkimpl)), nullptr);

    // Prepare both VAAPI and VA GST Elements
    // VAAPI
    auto vaapi_factory = gst_element_factory_find("vaapipostproc");
    auto sg_vaapi_factory = makeScopeGuard([vaapi_factory]() {
        if (vaapi_factory)
            gst_object_unref(vaapi_factory);
    });

    // VA
    auto va_factory = gst_element_factory_find("vapostproc");
    auto sg_va_factory = makeScopeGuard([va_factory]() {
        if (va_factory)
            gst_object_unref(va_factory);
    });

#ifdef ENABLE_VAAPI
    self->have_vaapi = vaapi_factory != nullptr;
    self->have_va = va_factory != nullptr;
#else
    self->have_vaapi = false;
    self->have_va = false;
#endif

    self->active_path = WatermarkPathNone;
    self->preferred_path = WatermarkPathNone;
    self->block_pad_source = WatermarkPathNone;
    self->is_active_nv12 = false;
    self->device = g_strdup(DEFAULT_DEVICE);
    self->obb = false;
    self->block_probe_id = 0;

    self->use_watermarkimpl_only = true;

    // Forward default device property to watermarkimpl
    g_object_set(self->watermarkimpl, "device", self->device, nullptr);
}

void gst_gva_watermark_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    GstGvaWatermark *gvawatermark = GST_GVA_WATERMARK(object);

    GST_DEBUG_OBJECT(gvawatermark, "set_property");

    switch (property_id) {
    case PROP_DEVICE:
        g_free(gvawatermark->device);
        gvawatermark->device = g_value_dup_string(value);
        g_object_set(gvawatermark->watermarkimpl, "device", gvawatermark->device, nullptr);
        break;
    case PROP_OBB:
        gvawatermark->obb = g_value_get_boolean(value);
        g_object_set(gvawatermark->watermarkimpl, "obb", gvawatermark->obb, nullptr);
        break;
    case PROP_DISPL_AVGFPS:
        gvawatermark->displ_avgfps = g_value_get_boolean(value);
        g_object_set(gvawatermark->watermarkimpl, "displ-avgfps", gvawatermark->displ_avgfps, nullptr);
        break;
    case PROP_DISPL_CFG:
        g_free(gvawatermark->displ_cfg);
        gvawatermark->displ_cfg = g_value_dup_string(value);
        g_object_set(gvawatermark->watermarkimpl, "displ-cfg", gvawatermark->displ_cfg, nullptr);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_gva_watermark_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    GstGvaWatermark *gvawatermark = GST_GVA_WATERMARK(object);

    GST_DEBUG_OBJECT(gvawatermark, "get_property");

    switch (property_id) {
    case PROP_DEVICE:
        g_value_set_string(value, gvawatermark->device);
        break;
    case PROP_OBB:
        g_value_set_boolean(value, gvawatermark->obb);
        break;
    case PROP_DISPL_AVGFPS:
        g_value_set_boolean(value, gvawatermark->displ_avgfps);
        break;
    case PROP_DISPL_CFG:
        g_value_set_string(value, gvawatermark->displ_cfg);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

void gst_gva_watermark_finalize(GObject *object) {
    GstGvaWatermark *gvawatermark = GST_GVA_WATERMARK(object);

    GST_DEBUG_OBJECT(gvawatermark, "finalize");

    if (gvawatermark->watermarkimpl) {
        gst_object_unref(gvawatermark->watermarkimpl);
        gvawatermark->watermarkimpl = nullptr;
    }

    g_free(gvawatermark->device);
    gvawatermark->device = nullptr;

    g_free(gvawatermark->displ_cfg);
    gvawatermark->displ_cfg = nullptr;

    G_OBJECT_CLASS(gst_gva_watermark_parent_class)->finalize(object);
}

static GstPadProbeReturn gva_watermark_sink_block_pad_probe(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    (void)pad;
    (void)user_data;

    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
        if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS || GST_EVENT_TYPE(event) == GST_EVENT_STREAM_START)
            return GST_PAD_PROBE_PASS;
    }
    return GST_PAD_PROBE_OK;
}

static void gva_watermark_block_sink(GstGvaWatermark *self, gboolean enable_block) {
    if (enable_block && self->block_probe_id != 0)
        return;
    if (!enable_block && self->block_probe_id == 0)
        return;

    GstPad *pad = nullptr;
    WatermarkPath pad_path = enable_block ? self->active_path : self->block_pad_source;

    if (pad_path == WatermarkPathTransparent) {
        pad = gst_element_get_static_pad(self->watermarkimpl, "sink");
    } else {
        pad = gst_element_get_static_pad(self->identity, "sink");
    }

    if (!pad)
        return;

    if (enable_block) {
        self->block_pad_source = pad_path;
        self->block_probe_id = gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
                                                 gva_watermark_sink_block_pad_probe, nullptr, nullptr);
    } else {
        gst_pad_remove_probe(pad, self->block_probe_id);
        self->block_probe_id = 0;
        self->block_pad_source = WatermarkPathNone;
    }

    gst_object_unref(pad);
    GST_DEBUG_OBJECT(self, "Sink block set to: %d (path=%d)", enable_block, pad_path);
}

static gboolean gva_watermark_set_src_pad(GstGvaWatermark *self, GstElement *src) {
    GstPad *pad = gst_element_get_static_pad(src, "src");
    gboolean ret = gst_ghost_pad_set_target(GST_GHOST_PAD_CAST(self->srcpad), pad);
    gst_object_unref(pad);
    return ret;
}

CapsFeature get_current_caps_feature(GstGvaWatermark *self) {
    if (!self->capsfilter) {
        return ANY_CAPS_FEATURE;
    }

    GstCaps *caps = nullptr;
    g_object_get(G_OBJECT(self->capsfilter), "caps", &caps, nullptr);
    if (caps == nullptr) {
        return ANY_CAPS_FEATURE;
    }
    return get_caps_feature(caps);
}

static gboolean G_GNUC_UNUSED link_videoconvert(GstGvaWatermark *self) {
    g_assert(self->active_path == WatermarkPathVaVaapi && "Supposed to be called in VAAPI path only");

    if (self->have_vaapi) {
        self->convert = gst_element_factory_make("videoconvert", nullptr);
        if (!self->convert) {
            GST_ELEMENT_ERROR(self, CORE, MISSING_PLUGIN, ("GStreamer installation is missing plugin videoconvert"),
                              (nullptr));
            return FALSE;
        }
        gst_bin_add(GST_BIN(self), self->convert);
        if (!gst_element_sync_state_with_parent(self->convert)) {
            GST_ELEMENT_ERROR(self, RESOURCE, NOT_FOUND, ("Couldn't sync videoconvert state with gvawatermark"),
                              (nullptr));
            return FALSE;
        }

        gst_element_unlink(self->identity, self->preproc);
        if (!gst_element_link(self->identity, self->convert) || !gst_element_link(self->convert, self->preproc)) {
            GST_ERROR_OBJECT(self, "videoconvert cannot be linked");
            return FALSE;
        }
    }
    return TRUE;
}

static G_GNUC_UNUSED gboolean unlink_videoconvert(GstGvaWatermark *self) {
    if (!self->convert || self->active_path != WatermarkPathVaVaapi)
        return TRUE;

    gst_element_unlink(self->identity, self->convert);
    gst_element_unlink(self->convert, self->preproc);
    if (!gst_element_link(self->identity, self->preproc)) {
        GST_ERROR_OBJECT(self, "Unable to link identity to va(api)postproc after removing videoconvert");
        return FALSE;
    }

    gst_element_set_state(self->convert, GST_STATE_NULL);
    gst_bin_remove(GST_BIN(self), self->convert);
    self->convert = nullptr;

    return TRUE;
}

static gboolean gva_watermark_link_elements(GstGvaWatermark *self, GstElement *src, GstElement *dest) {
    if (!gst_element_link(src, dest)) {
        GST_ELEMENT_ERROR(self, RESOURCE, NOT_FOUND,
                          ("Couldn't link element %s to %s", GST_ELEMENT_NAME(src), GST_ELEMENT_NAME(dest)), (nullptr));
        return FALSE;
    }
    return TRUE;
}

// VA path:
// |ghost sink| -> <identity> -> <vapostproc> -> <capsfilter> -> <watermarkimpl> -> <vapostproc> -> |ghost src|
// VA-API path:
// |ghost sink| -> <identity> -> <vaapipostproc> -> <capsfilter> -> <watermarkimpl> -> <vaapipostproc> -> |ghost src|
static gboolean gva_watermark_link_vavaapi_path(GstGvaWatermark *self, CapsFeature in_mem_type) {
    if (self->have_vaapi && in_mem_type == VA_SURFACE_CAPS_FEATURE) {
        self->preproc = gst_element_factory_make("vaapipostproc", nullptr);
        self->postproc = gst_element_factory_make("vaapipostproc", nullptr);
        self->capsfilter = gst_element_factory_make("capsfilter", nullptr);
    } else if (self->have_va && in_mem_type == VA_MEMORY_CAPS_FEATURE) {
        self->preproc = gst_element_factory_make("vapostproc", nullptr);
        self->postproc = gst_element_factory_make("vapostproc", nullptr);
        self->capsfilter = gst_element_factory_make("capsfilter", nullptr);
    } else {
        GST_ELEMENT_ERROR(self, CORE, MISSING_PLUGIN,
                          ("GStreamer installation is missing plugins of VA-API or VA path"), (nullptr));
        return FALSE;
    }

    if (!self->preproc || !self->postproc || !self->capsfilter) {
        GST_ELEMENT_ERROR(self, CORE, MISSING_PLUGIN,
                          ("GStreamer installation is missing plugins of VA-API or VA path"), (nullptr));
        return FALSE;
    }

    gst_bin_add_many(GST_BIN(self), self->preproc, self->postproc, self->capsfilter, nullptr);

    gst_util_set_object_arg(G_OBJECT(self->capsfilter), "caps", get_caps_str_with_feature(in_mem_type));

    if (!gst_bin_sync_children_states(GST_BIN(self))) {
        GST_ELEMENT_ERROR(self, RESOURCE, NOT_FOUND, ("Couldn't sync elements state with parent bin"), (nullptr));
        return FALSE;
    }

    if (!gst_element_link(self->identity, self->preproc)) {
        GST_INFO_OBJECT(self, "va(api)postproc cannot be linked, unsupported format");
        return FALSE;
    }

    gboolean ret = gva_watermark_link_elements(self, self->preproc, self->capsfilter) &&
                   gva_watermark_link_elements(self, self->capsfilter, self->watermarkimpl) &&
                   gva_watermark_link_elements(self, self->watermarkimpl, self->postproc);

    if (!gva_watermark_set_src_pad(self, self->postproc)) {
        GST_ERROR_OBJECT(self, "Couldn't set target for src ghost pad");
        return FALSE;
    }

    if (!ret)
        return FALSE;

    self->active_path = WatermarkPathVaVaapi;
    return TRUE;
}

static void gva_watermark_unlink_vavaapi_path(GstGvaWatermark *self) {
    if (self->convert) {
        gst_element_unlink(self->identity, self->convert);
        gst_element_unlink(self->convert, self->preproc);
    } else {
        gst_element_unlink(self->identity, self->preproc);
    }
    gst_element_unlink(self->preproc, self->capsfilter);
    gst_element_unlink(self->capsfilter, self->watermarkimpl);
    gst_element_unlink(self->watermarkimpl, self->postproc);
    gst_ghost_pad_set_target(GST_GHOST_PAD_CAST(self->srcpad), NULL);

    if (self->convert) {
        gst_element_set_state(self->convert, GST_STATE_NULL);
    }
    gst_element_set_state(self->preproc, GST_STATE_NULL);
    gst_element_set_state(self->capsfilter, GST_STATE_NULL);
    gst_element_set_state(self->postproc, GST_STATE_NULL);

    gst_bin_remove_many(GST_BIN(self), self->preproc, self->capsfilter, self->postproc, nullptr);

    if (self->convert) {
        gst_bin_remove(GST_BIN(self), self->convert);
    }

    self->convert = nullptr;
    self->preproc = nullptr;
    self->capsfilter = nullptr;
    self->postproc = nullptr;
}

// Transparent path:
// |ghost sink| -> <watermarkimpl> -> |ghost src|
// Skips identity; gvawatermarkimpl handles everything directly.
static gboolean gva_watermark_link_transparent_path(GstGvaWatermark *self) {
    if (!gst_bin_sync_children_states(GST_BIN(self))) {
        GST_ELEMENT_ERROR(self, RESOURCE, NOT_FOUND, ("Couldn't sync elements state with parent bin"), (nullptr));
        return FALSE;
    }

    // Connect sink ghost pad directly to watermarkimpl sink
    GstPad *wmimpl_sink = gst_element_get_static_pad(self->watermarkimpl, "sink");
    if (!gst_ghost_pad_set_target(GST_GHOST_PAD_CAST(self->sinkpad), wmimpl_sink)) {
        GST_ERROR_OBJECT(self, "Couldn't set target for sink ghost pad");
        gst_object_unref(wmimpl_sink);
        return FALSE;
    }
    gst_object_unref(wmimpl_sink);

    // Connect src ghost pad directly to watermarkimpl src
    if (!gva_watermark_set_src_pad(self, self->watermarkimpl)) {
        GST_ERROR_OBJECT(self, "Couldn't set target for src ghost pad");
        return FALSE;
    }

    self->active_path = WatermarkPathTransparent;
    GST_INFO_OBJECT(self, "Transparent path linked (identity bypassed)");
    return TRUE;
}

static void gva_watermark_unlink_transparent_path(GstGvaWatermark *self) {
    gst_ghost_pad_set_target(GST_GHOST_PAD_CAST(self->sinkpad), NULL);
    gst_ghost_pad_set_target(GST_GHOST_PAD_CAST(self->srcpad), NULL);
}

// Direct path:
// |ghost sink| -> <identity> -> <watermarkimpl> -> |ghost src|
static gboolean gva_watermark_link_direct_path(GstGvaWatermark *self, bool use_postproc = false,
                                               CapsFeature in_mem_type = VA_MEMORY_CAPS_FEATURE) {
    if (use_postproc) {
        if (self->have_vaapi && in_mem_type == VA_SURFACE_CAPS_FEATURE) {
            self->postproc = gst_element_factory_make("vaapipostproc", nullptr);
            if (!self->postproc) {
                GST_ERROR_OBJECT(self, "Could not create vaapipostproc instance");
            }
        } else if (self->have_va && in_mem_type == VA_MEMORY_CAPS_FEATURE) {
            self->postproc = gst_element_factory_make("vapostproc", nullptr);
            if (!self->postproc) {
                GST_ERROR_OBJECT(self, "Could not create vapostproc instance");
            }
        } else if ((self->have_vaapi || self->have_va) && in_mem_type == SYSTEM_MEMORY_CAPS_FEATURE) {
            // DMA case handled as VA(API)
            // Check gva_watermark_start() for identitySrcFeature == DMA_BUF_CAPS_FEATURE.
            self->postproc = self->have_va ? gst_element_factory_make("vapostproc", nullptr)
                                           : gst_element_factory_make("vaapipostproc", nullptr);
            if (!self->postproc) {
                GST_ERROR_OBJECT(self, "Could not create vapostproc instance");
            }
        } else {
            GST_ELEMENT_ERROR(self, CORE, MISSING_PLUGIN,
                              ("GStreamer installation is missing plugins of VA-API or VA path"), (nullptr));
            return FALSE;
        }
        gst_bin_add(GST_BIN(self), self->postproc);
    }

    if (!gst_bin_sync_children_states(GST_BIN(self))) {
        GST_ELEMENT_ERROR(self, RESOURCE, NOT_FOUND, ("Couldn't sync elements state with parent bin"), (nullptr));
        return FALSE;
    }

    if (!gva_watermark_link_elements(self, self->identity, self->watermarkimpl))
        return FALSE;

    if (use_postproc && !gva_watermark_link_elements(self, self->watermarkimpl, self->postproc)) {
        return FALSE;
    }

    gboolean src_pad_is_set = (use_postproc) ? gva_watermark_set_src_pad(self, self->postproc)
                                             : gva_watermark_set_src_pad(self, self->watermarkimpl);

    if (!src_pad_is_set) {
        GST_ERROR_OBJECT(self, "Couldn't set target for src ghost pad");
        return FALSE;
    }

    self->active_path = WatermarkPathDirect;
    return TRUE;
}

static void gva_watermark_unlink_direct_path(GstGvaWatermark *self) {
    gst_element_unlink(self->identity, self->watermarkimpl);
    gst_ghost_pad_set_target(GST_GHOST_PAD_CAST(self->srcpad), NULL);

    if (self->postproc) {
        gst_element_unlink(self->watermarkimpl, self->postproc);
        gst_element_set_state(self->postproc, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(self), self->postproc);
        self->postproc = nullptr;
    }
}

static gboolean gva_watermark_switch_path(GstGvaWatermark *self, enum WatermarkPath path, CapsFeature in_mem_type) {
    g_assert(path != WatermarkPathNone && "Cannot switch path to None");
    GST_DEBUG_OBJECT(self, "Switching to path: %d, memory type: %d", path, in_mem_type);

    if (self->active_path == path)
        return TRUE;

    gva_watermark_block_sink(self, TRUE);

    switch (self->active_path) {
    case WatermarkPathDirect:
        gva_watermark_unlink_direct_path(self);
        break;
    case WatermarkPathVaVaapi:
        gva_watermark_unlink_vavaapi_path(self);
        break;
    case WatermarkPathTransparent:
        gva_watermark_unlink_transparent_path(self);
        break;
    case WatermarkPathNone:
        break;
    default:
        GST_ERROR_OBJECT(self, "Unexpected path received during the gvawatermark unlink");
        gva_watermark_block_sink(self, FALSE);
        return FALSE;
    }

    gboolean result = FALSE;
    switch (path) {
    case WatermarkPathDirect:
        result = gva_watermark_link_direct_path(self, self->is_active_nv12 && (self->have_vaapi || self->have_va),
                                                in_mem_type);
        break;
    case WatermarkPathVaVaapi:
        result = gva_watermark_link_vavaapi_path(self, in_mem_type);
        break;
    case WatermarkPathTransparent:
        result = gva_watermark_link_transparent_path(self);
        break;
    case WatermarkPathNone:
    default:
        GST_ERROR_OBJECT(self, "Unexpected path received during the gvawatermark link");
        gva_watermark_block_sink(self, FALSE);
        return FALSE;
    }

    gva_watermark_block_sink(self, FALSE);
    return result;
}

static gboolean gst_gva_watermark_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    GstGvaWatermark *self = GST_GVA_WATERMARK(parent);

    if (!(self->use_watermarkimpl_only)) {

        switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS: {
            GstCaps *incaps;
            gst_event_parse_caps(event, &incaps);
            GST_DEBUG_OBJECT(parent, "Got CAPS event, caps: %" GST_PTR_FORMAT, incaps);

            auto target_memtype = get_caps_feature(incaps);

            // Non-system memory (WatermarkPathVaVaapi path) accepts BGRx images only
            if ((target_memtype != SYSTEM_MEMORY_CAPS_FEATURE) && !is_caps_format_equal(incaps, "BGRx"))
                if (!unlink_videoconvert(self))
                    return FALSE;

            self->is_active_nv12 = is_caps_format_equal(incaps, "NV12");

            // save preferred path here to change on GST_EVENT_SEGMENT if needed
            if (target_memtype == SYSTEM_MEMORY_CAPS_FEATURE)
                self->preferred_path = WatermarkPathDirect;
            else
                self->preferred_path = WatermarkPathVaVaapi;

        } break;
        case GST_EVENT_SEGMENT: {
            if (self->preferred_path == WatermarkPathDirect)
                if (!gva_watermark_switch_path(self, WatermarkPathDirect, SYSTEM_MEMORY_CAPS_FEATURE))
                    return FALSE;
        } break;
        default:
            break;
        }
    }

    return gst_pad_event_default(pad, parent, event);
}

static gboolean gva_watermark_start(GstGvaWatermark *self) {
    if (!self->watermarkimpl || !self->identity)
        return FALSE;

    // Set sink ghost pad to input identity element
    GstPad *pad = gst_element_get_static_pad(self->identity, "sink");
    gboolean ret = gst_ghost_pad_set_target(GST_GHOST_PAD_CAST(self->sinkpad), pad);
    gst_object_unref(pad);

    if (!ret)
        return FALSE;

    // Get src pad type of identity element to check postproc sink type
    GstPad *identitySrc = gst_element_get_static_pad(self->identity, "src");
    CapsFeature identitySrcFeature = get_caps_feature(gst_pad_query_caps(identitySrc, nullptr));
    CapsFeature in_memory_type = ANY_CAPS_FEATURE;

    if (identitySrcFeature == VA_SURFACE_CAPS_FEATURE && self->have_vaapi) {
        self->have_va = false;
        in_memory_type = VA_SURFACE_CAPS_FEATURE;
    } else if ((identitySrcFeature == VA_MEMORY_CAPS_FEATURE || identitySrcFeature == DMA_BUF_CAPS_FEATURE) &&
               self->have_va) {
        self->have_vaapi = false;
        // Prefered GST-VA path, see DEVICE_GPU_AUTOSELECTED
        in_memory_type = VA_MEMORY_CAPS_FEATURE;
    } else {
        self->have_va = false;
        self->have_vaapi = false;
    }

    if (self->use_watermarkimpl_only)
        return gva_watermark_switch_path(self, WatermarkPathTransparent, in_memory_type);

    if (self->have_vaapi || self->have_va) {
        if (gva_watermark_switch_path(self, WatermarkPathVaVaapi, in_memory_type) && link_videoconvert(self))
            return TRUE;
        else
            GST_INFO_OBJECT(self, "Unsopported format on sink pad, switching to direct path");
    } else {
        GST_INFO_OBJECT(self, "va(api)postproc is not found, switching to direct path");
    }
    return gva_watermark_switch_path(self, WatermarkPathDirect, SYSTEM_MEMORY_CAPS_FEATURE);
}

static GstStateChangeReturn gva_watermark_change_state(GstElement *element, GstStateChange transition) {
    GstGvaWatermark *self = GST_GVA_WATERMARK(element);

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY: {
        if (!gva_watermark_start(self))
            return GST_STATE_CHANGE_FAILURE;
    } break;
    default:
        break;
    }

    return GST_ELEMENT_CLASS(gst_gva_watermark_parent_class)->change_state(element, transition);
}

static gboolean plugin_init(GstPlugin *plugin) {
    if (!gst_element_register(plugin, "gvawatermark", GST_RANK_NONE, GST_TYPE_GVA_WATERMARK))
        return FALSE;

    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, gvawatermark, PRODUCT_FULL_NAME " gvawatermark element",
                  plugin_init, PLUGIN_VERSION, PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
