/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvaframeselect.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <gst/video/video.h>

#include "frame_selector.hpp"
#include "gva_caps.h"

constexpr guint DEFAULT_SUB_STREAM_FRAMES = 900;
constexpr guint DEFAULT_FRAME_BUDGET = 8;
constexpr GstClockTime JPEG_ENCODE_TIMEOUT = 5 * GST_SECOND;

enum { PROP_0, PROP_SUB_STREAM_FRAMES, PROP_FRAME_BUDGET, PROP_OUTPUT_DIR };

struct GstGvaFrameSelect {
    GstBaseTransform parent;
    guint sub_stream_frames;
    guint frame_budget;
    guint frame_index;
    guint64 stream_frame_number;
    gchar *output_dir;
};

struct GstGvaFrameSelectClass {
    GstBaseTransformClass parent_class;
};

G_DEFINE_TYPE(GstGvaFrameSelect, gst_gva_frame_select, GST_TYPE_BASE_TRANSFORM)

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVA_CAPS));
static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVA_CAPS));

static void gst_gva_frame_select_set_property(GObject *object, guint property_id, const GValue *value,
                                              GParamSpec *pspec) {
    auto *self = reinterpret_cast<GstGvaFrameSelect *>(object);
    switch (property_id) {
    case PROP_SUB_STREAM_FRAMES:
        self->sub_stream_frames = g_value_get_uint(value);
        self->frame_index = 0;
        break;
    case PROP_FRAME_BUDGET:
        self->frame_budget = g_value_get_uint(value);
        break;
    case PROP_OUTPUT_DIR:
        GST_OBJECT_LOCK(self);
        g_free(self->output_dir);
        self->output_dir = g_value_dup_string(value);
        GST_OBJECT_UNLOCK(self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void gst_gva_frame_select_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    auto *self = reinterpret_cast<GstGvaFrameSelect *>(object);
    switch (property_id) {
    case PROP_SUB_STREAM_FRAMES:
        g_value_set_uint(value, self->sub_stream_frames);
        break;
    case PROP_FRAME_BUDGET:
        g_value_set_uint(value, self->frame_budget);
        break;
    case PROP_OUTPUT_DIR:
        GST_OBJECT_LOCK(self);
        g_value_set_string(value, self->output_dir);
        GST_OBJECT_UNLOCK(self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void gst_gva_frame_select_finalize(GObject *object) {
    auto *self = reinterpret_cast<GstGvaFrameSelect *>(object);
    g_free(self->output_dir);
    G_OBJECT_CLASS(gst_gva_frame_select_parent_class)->finalize(object);
}

static gboolean gst_gva_frame_select_start(GstBaseTransform *transform) {
    auto *self = reinterpret_cast<GstGvaFrameSelect *>(transform);
    self->frame_index = 0;
    self->stream_frame_number = 0;
    return TRUE;
}

static void save_jpeg(GstGvaFrameSelect *self, GstBuffer *buffer, const gchar *output_dir, guint64 frame_number) {
    if (g_mkdir_with_parents(output_dir, 0755) != 0) {
        GST_ELEMENT_WARNING(self, RESOURCE, OPEN_WRITE, ("Could not create output directory '%s'", output_dir), (NULL));
        return;
    }

    GstCaps *caps = gst_pad_get_current_caps(GST_BASE_TRANSFORM_SINK_PAD(self));
    GstSample *sample = gst_sample_new(buffer, caps, nullptr, nullptr);
    GstCaps *jpeg_caps = gst_caps_new_empty_simple("image/jpeg");
    GError *error = nullptr;
    GstSample *jpeg = gst_video_convert_sample(sample, jpeg_caps, JPEG_ENCODE_TIMEOUT, &error);
    gst_caps_unref(jpeg_caps);
    gst_sample_unref(sample);
    if (caps)
        gst_caps_unref(caps);

    if (!jpeg) {
        GST_ELEMENT_WARNING(self, STREAM, ENCODE,
                            ("Could not encode frame %" G_GUINT64_FORMAT " to JPEG", frame_number),
                            ("%s", error ? error->message : "unknown error"));
        g_clear_error(&error);
        return;
    }

    GstBuffer *jpeg_buffer = gst_sample_get_buffer(jpeg);
    GstMapInfo map;
    if (jpeg_buffer && gst_buffer_map(jpeg_buffer, &map, GST_MAP_READ)) {
        gchar *file_name = g_strdup_printf("frame_%08" G_GUINT64_FORMAT ".jpg", frame_number);
        gchar *path = g_build_filename(output_dir, file_name, nullptr);
        if (!g_file_set_contents(path, reinterpret_cast<const gchar *>(map.data), static_cast<gssize>(map.size),
                                 &error)) {
            GST_ELEMENT_WARNING(self, RESOURCE, WRITE, ("Could not write '%s'", path), ("%s", error->message));
            g_clear_error(&error);
        }
        g_free(path);
        g_free(file_name);
        gst_buffer_unmap(jpeg_buffer, &map);
    }
    gst_sample_unref(jpeg);
}

static GstFlowReturn gst_gva_frame_select_transform_ip(GstBaseTransform *transform, GstBuffer *buffer) {
    auto *self = reinterpret_cast<GstGvaFrameSelect *>(transform);
    // Counts every input frame from stream start; unlike frame_index it is not reset on DISCONT.
    const guint64 stream_frame_number = self->stream_frame_number++;
    if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT))
        self->frame_index = 0;

    const guint index = self->frame_index;
    if (++self->frame_index == self->sub_stream_frames)
        self->frame_index = 0;
    const auto selected = gvaframeselect::select_indices(self->sub_stream_frames, self->frame_budget);
    if (!std::binary_search(selected.begin(), selected.end(), static_cast<std::size_t>(index)))
        return GST_BASE_TRANSFORM_FLOW_DROPPED;

    // For video, GstBuffer offset is defined as the frame number.
    GST_BUFFER_OFFSET(buffer) = stream_frame_number;

    GST_OBJECT_LOCK(self);
    gchar *output_dir = g_strdup(self->output_dir);
    GST_OBJECT_UNLOCK(self);
    if (output_dir && *output_dir)
        save_jpeg(self, buffer, output_dir, stream_frame_number);
    g_free(output_dir);
    return GST_FLOW_OK;
}

static gboolean gst_gva_frame_select_sink_event(GstBaseTransform *transform, GstEvent *event) {
    auto *self = reinterpret_cast<GstGvaFrameSelect *>(transform);
    if (GST_EVENT_TYPE(event) == GST_EVENT_STREAM_START)
        self->stream_frame_number = 0;
    return GST_BASE_TRANSFORM_CLASS(gst_gva_frame_select_parent_class)->sink_event(transform, event);
}

static void gst_gva_frame_select_class_init(GstGvaFrameSelectClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_set_static_metadata(element_class, "Frame Select", "Video/Filter",
                                          "Selects a fixed frame budget from each event sub-stream",
                                          "Intel Corporation");

    object_class->set_property = gst_gva_frame_select_set_property;
    object_class->get_property = gst_gva_frame_select_get_property;
    object_class->finalize = gst_gva_frame_select_finalize;
    transform_class->start = GST_DEBUG_FUNCPTR(gst_gva_frame_select_start);
    transform_class->sink_event = GST_DEBUG_FUNCPTR(gst_gva_frame_select_sink_event);
    transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gva_frame_select_transform_ip);

    g_object_class_install_property(
        object_class, PROP_SUB_STREAM_FRAMES,
        g_param_spec_uint("sub-stream-frames", "Sub-stream Frames",
                          "Length N, in input frames, of each selection block; the pattern restarts every N frames "
                          "and on DISCONT",
                          1, G_MAXUINT, DEFAULT_SUB_STREAM_FRAMES,
                          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, PROP_FRAME_BUDGET,
        g_param_spec_uint("frame-budget", "Frame Budget",
                          "Maximum k frames forwarded per full block; selected positions are evenly spaced, with the "
                          "first frame used when k is 1",
                          1, G_MAXUINT, DEFAULT_FRAME_BUDGET,
                          static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, PROP_OUTPUT_DIR,
        g_param_spec_string("output-dir", "Output Directory",
                            "If set, each forwarded frame is also saved to this directory as "
                            "frame_<stream frame number>.jpg; frame numbers count all input frames from stream start",
                            nullptr, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_gva_frame_select_init(GstGvaFrameSelect *self) {
    self->sub_stream_frames = DEFAULT_SUB_STREAM_FRAMES;
    self->frame_budget = DEFAULT_FRAME_BUDGET;
    self->frame_index = 0;
    self->stream_frame_number = 0;
    self->output_dir = nullptr;
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), FALSE);
}
