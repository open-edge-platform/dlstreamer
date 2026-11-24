/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvaroiscale.h"

#include <vector>

#include <glib/gstdio.h>
#include <gst/video/video.h>

#include "roi_scale.hpp"

constexpr guint DEFAULT_OUT_WIDTH = 448;
constexpr guint DEFAULT_OUT_HEIGHT = 448;
constexpr guint MAX_OUT_DIMENSION = 8192;
constexpr gdouble DEFAULT_CONFIDENCE_THRESHOLD = 0.0;
constexpr GstClockTime JPEG_ENCODE_TIMEOUT = 5 * GST_SECOND;

enum { PROP_0, PROP_OUT_WIDTH, PROP_OUT_HEIGHT, PROP_OUTPUT_DIR, PROP_TARGET_CLASSES, PROP_CONFIDENCE_THRESHOLD };

struct GstGvaRoiScale {
    GstBaseTransform parent;
    guint out_width;
    guint out_height;
    gdouble confidence_threshold;
    guint64 output_frame_number;
    gchar *output_dir;
    gchar *target_classes_csv;
    GArray *target_classes;
    GstVideoInfo input_info;
    GstVideoInfo output_info;
    GstBuffer *pending_input;
    std::vector<gvaroiscale::Roi> pending_rois;
    std::size_t next_roi_index;
};

struct GstGvaRoiScaleClass {
    GstBaseTransformClass parent_class;
};

G_DEFINE_TYPE(GstGvaRoiScale, gst_gva_roi_scale, GST_TYPE_BASE_TRANSFORM)

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw, format=(string)RGB, width=(int)[1,8192], height=(int)[1,8192]"));
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw, format=(string)RGB, width=(int)[1,8192], height=(int)[1,8192]"));

static GstCaps *gst_gva_roi_scale_transform_caps(GstBaseTransform *transform, GstPadDirection direction, GstCaps *caps,
                                                 GstCaps *filter) {
    auto *self = reinterpret_cast<GstGvaRoiScale *>(transform);
    GstCaps *result = gst_caps_copy(caps);
    for (guint i = 0; i < gst_caps_get_size(result); ++i) {
        GstStructure *structure = gst_caps_get_structure(result, i);
        if (direction == GST_PAD_SINK) {
            gst_structure_set(structure, "width", G_TYPE_INT, static_cast<gint>(self->out_width), "height", G_TYPE_INT,
                              static_cast<gint>(self->out_height), nullptr);
        } else {
            gst_structure_remove_field(structure, "width");
            gst_structure_remove_field(structure, "height");
        }
    }
    if (filter) {
        GstCaps *intersection = gst_caps_intersect_full(filter, result, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(result);
        result = intersection;
    }
    return result;
}

static gboolean gst_gva_roi_scale_set_caps(GstBaseTransform *transform, GstCaps *input_caps, GstCaps *output_caps) {
    auto *self = reinterpret_cast<GstGvaRoiScale *>(transform);
    if (!gst_video_info_from_caps(&self->input_info, input_caps) ||
        !gst_video_info_from_caps(&self->output_info, output_caps) ||
        GST_VIDEO_INFO_FORMAT(&self->input_info) != GST_VIDEO_FORMAT_RGB ||
        GST_VIDEO_INFO_FORMAT(&self->output_info) != GST_VIDEO_FORMAT_RGB ||
        GST_VIDEO_INFO_WIDTH(&self->output_info) != self->out_width ||
        GST_VIDEO_INFO_HEIGHT(&self->output_info) != self->out_height)
        return FALSE;
    return TRUE;
}

static GArray *parse_target_classes(const gchar *csv) {
    GArray *classes = g_array_new(FALSE, FALSE, sizeof(GQuark));
    if (!csv || !*csv)
        return classes;

    gchar **labels = g_strsplit(csv, ",", -1);
    for (gchar **label = labels; *label; ++label) {
        gchar *trimmed = g_strstrip(*label);
        if (!*trimmed)
            continue;
        const GQuark quark = g_quark_from_string(trimmed);
        g_array_append_val(classes, quark);
    }
    g_strfreev(labels);
    return classes;
}

static void save_jpeg(GstGvaRoiScale *self, GstBuffer *buffer, const gchar *output_dir, guint64 frame_number) {
    if (g_mkdir_with_parents(output_dir, 0755) != 0) {
        GST_ELEMENT_WARNING(self, RESOURCE, OPEN_WRITE, ("Could not create output directory '%s'", output_dir), (NULL));
        return;
    }

    GstCaps *caps = gst_pad_get_current_caps(GST_BASE_TRANSFORM_SRC_PAD(self));
    if (!caps) {
        GST_ELEMENT_WARNING(self, STREAM, ENCODE,
                            ("Could not get output caps for frame %" G_GUINT64_FORMAT, frame_number), (NULL));
        return;
    }
    GstSample *sample = gst_sample_new(buffer, caps, nullptr, nullptr);
    gst_caps_unref(caps);
    GstCaps *jpeg_caps = gst_caps_new_empty_simple("image/jpeg");
    GError *error = nullptr;
    GstSample *jpeg = gst_video_convert_sample(sample, jpeg_caps, JPEG_ENCODE_TIMEOUT, &error);
    gst_caps_unref(jpeg_caps);
    gst_sample_unref(sample);
    if (!jpeg) {
        GST_ELEMENT_WARNING(self, STREAM, ENCODE,
                            ("Could not encode frame %" G_GUINT64_FORMAT " to JPEG", frame_number),
                            ("%s", error ? error->message : "unknown error"));
        g_clear_error(&error);
        return;
    }

    GstBuffer *jpeg_buffer = gst_sample_get_buffer(jpeg);
    GstMapInfo map = GST_MAP_INFO_INIT;
    if (jpeg_buffer && gst_buffer_map(jpeg_buffer, &map, GST_MAP_READ)) {
        gchar *file_name = g_strdup_printf("frame_%08" G_GUINT64_FORMAT ".jpg", frame_number);
        gchar *path = g_build_filename(output_dir, file_name, nullptr);
        if (!g_file_set_contents(path, reinterpret_cast<const gchar *>(map.data), static_cast<gssize>(map.size),
                                 &error)) {
            GST_ELEMENT_WARNING(self, RESOURCE, WRITE, ("Could not write '%s'", path),
                                ("%s", error ? error->message : "unknown error"));
            g_clear_error(&error);
        }
        g_free(path);
        g_free(file_name);
        gst_buffer_unmap(jpeg_buffer, &map);
    }
    gst_sample_unref(jpeg);
}

static void clear_pending_input(GstGvaRoiScale *self) {
    if (self->pending_input)
        gst_buffer_unref(self->pending_input);
    self->pending_input = nullptr;
    self->pending_rois.clear();
    self->next_roi_index = 0;
}

// The crop is a new buffer, so the source ODMtd is re-created on it in output-frame coordinates.
static void add_roi_metadata(GstGvaRoiScale *self, GstBuffer *buffer, const gvaroiscale::Roi &roi,
                             const GstVideoRectangle &content_rect) {
    GstAnalyticsRelationMeta *rmeta = gst_buffer_add_analytics_relation_meta(buffer);
    GstAnalyticsODMtd od_mtd;
    if (!rmeta || !gst_analytics_relation_meta_add_od_mtd(rmeta, roi.label, content_rect.x, content_rect.y,
                                                          content_rect.w, content_rect.h, roi.confidence, &od_mtd))
        GST_WARNING_OBJECT(self, "Could not attach ROI detection metadata");
}

static GstFlowReturn gst_gva_roi_scale_generate_output(GstBaseTransform *transform, GstBuffer **outbuf) {
    GstGvaRoiScale *self = reinterpret_cast<GstGvaRoiScale *>(transform);
    *outbuf = nullptr;

    if (!self->pending_input) {
        GstBuffer *input = transform->queued_buf;
        transform->queued_buf = nullptr;
        if (!input)
            return GST_BASE_TRANSFORM_FLOW_DROPPED;

        self->pending_input = input;
        gdouble confidence_threshold;
        GST_OBJECT_LOCK(self);
        GArray *target_classes = g_array_ref(self->target_classes);
        confidence_threshold = self->confidence_threshold;
        GST_OBJECT_UNLOCK(self);
        self->pending_rois = gvaroiscale::collect_rois(input, target_classes, GST_VIDEO_INFO_WIDTH(&self->input_info),
                                                       GST_VIDEO_INFO_HEIGHT(&self->input_info),
                                                       static_cast<float>(confidence_threshold));
        g_array_unref(target_classes);
        if (self->pending_rois.empty()) {
            clear_pending_input(self);
            return GST_BASE_TRANSFORM_FLOW_DROPPED;
        }
    }

    const std::size_t roi_index = self->next_roi_index++;
    const std::size_t roi_count = self->pending_rois.size();
    const gvaroiscale::Roi &roi = self->pending_rois[roi_index];
    GstBuffer *output = nullptr;
    GstVideoRectangle content_rect;
    const auto result = gvaroiscale::roi_scale_buffer(self->pending_input, self->input_info, roi, self->out_width,
                                                      self->out_height, &output, nullptr, &content_rect);
    if (result != gvaroiscale::RoiScaleResult::Ok) {
        clear_pending_input(self);
        return result == gvaroiscale::RoiScaleResult::NoRoi ? GST_BASE_TRANSFORM_FLOW_DROPPED : GST_FLOW_ERROR;
    }

    GST_BUFFER_FLAGS(output) = GST_BUFFER_FLAGS(self->pending_input);
    if (roi_index > 0)
        GST_BUFFER_FLAG_UNSET(output, GST_BUFFER_FLAG_DISCONT);
    add_roi_metadata(self, output, roi, content_rect);

    const guint64 output_frame_number = self->output_frame_number++;
    GST_OBJECT_LOCK(self);
    gchar *output_dir = g_strdup(self->output_dir);
    GST_OBJECT_UNLOCK(self);
    if (output_dir && *output_dir)
        save_jpeg(self, output, output_dir, output_frame_number);
    g_free(output_dir);

    *outbuf = output;
    if (self->next_roi_index == roi_count)
        clear_pending_input(self);
    return GST_FLOW_OK;
}

static void gst_gva_roi_scale_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    auto *self = reinterpret_cast<GstGvaRoiScale *>(object);
    switch (property_id) {
    case PROP_OUT_WIDTH:
        self->out_width = g_value_get_uint(value);
        break;
    case PROP_OUT_HEIGHT:
        self->out_height = g_value_get_uint(value);
        break;
    case PROP_CONFIDENCE_THRESHOLD:
        GST_OBJECT_LOCK(self);
        self->confidence_threshold = g_value_get_double(value);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_OUTPUT_DIR:
        GST_OBJECT_LOCK(self);
        g_free(self->output_dir);
        self->output_dir = g_value_dup_string(value);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_TARGET_CLASSES: {
        gchar *target_classes_csv = g_value_dup_string(value);
        GArray *target_classes = parse_target_classes(target_classes_csv);
        GST_OBJECT_LOCK(self);
        g_free(self->target_classes_csv);
        g_array_unref(self->target_classes);
        self->target_classes_csv = target_classes_csv;
        self->target_classes = target_classes;
        GST_OBJECT_UNLOCK(self);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        return;
    }
    gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(self));
}

static void gst_gva_roi_scale_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    auto *self = reinterpret_cast<GstGvaRoiScale *>(object);
    switch (property_id) {
    case PROP_OUT_WIDTH:
        g_value_set_uint(value, self->out_width);
        break;
    case PROP_OUT_HEIGHT:
        g_value_set_uint(value, self->out_height);
        break;
    case PROP_CONFIDENCE_THRESHOLD:
        GST_OBJECT_LOCK(self);
        g_value_set_double(value, self->confidence_threshold);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_OUTPUT_DIR:
        GST_OBJECT_LOCK(self);
        g_value_set_string(value, self->output_dir);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_TARGET_CLASSES:
        GST_OBJECT_LOCK(self);
        g_value_set_string(value, self->target_classes_csv);
        GST_OBJECT_UNLOCK(self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void gst_gva_roi_scale_finalize(GObject *object) {
    auto *self = reinterpret_cast<GstGvaRoiScale *>(object);
    clear_pending_input(self);
    g_free(self->output_dir);
    g_free(self->target_classes_csv);
    g_array_unref(self->target_classes);
    G_OBJECT_CLASS(gst_gva_roi_scale_parent_class)->finalize(object);
}

static gboolean gst_gva_roi_scale_start(GstBaseTransform *transform) {
    auto *self = reinterpret_cast<GstGvaRoiScale *>(transform);
    clear_pending_input(self);
    self->output_frame_number = 0;
    return TRUE;
}

static gboolean gst_gva_roi_scale_stop(GstBaseTransform *transform) {
    clear_pending_input(reinterpret_cast<GstGvaRoiScale *>(transform));
    return TRUE;
}

static gboolean gst_gva_roi_scale_sink_event(GstBaseTransform *transform, GstEvent *event) {
    auto *self = reinterpret_cast<GstGvaRoiScale *>(transform);
    if (GST_EVENT_TYPE(event) == GST_EVENT_STREAM_START) {
        clear_pending_input(self);
        self->output_frame_number = 0;
    }
    return GST_BASE_TRANSFORM_CLASS(gst_gva_roi_scale_parent_class)->sink_event(transform, event);
}

static void gst_gva_roi_scale_class_init(GstGvaRoiScaleClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_set_static_metadata(element_class, "ROI Scale", "Video/Filter",
                                          "Crops the detected ROI and scales it to the configured VLM input size",
                                          "Intel Corporation");
    object_class->set_property = gst_gva_roi_scale_set_property;
    object_class->get_property = gst_gva_roi_scale_get_property;
    object_class->finalize = gst_gva_roi_scale_finalize;
    transform_class->start = GST_DEBUG_FUNCPTR(gst_gva_roi_scale_start);
    transform_class->stop = GST_DEBUG_FUNCPTR(gst_gva_roi_scale_stop);
    transform_class->sink_event = GST_DEBUG_FUNCPTR(gst_gva_roi_scale_sink_event);
    transform_class->transform_caps = GST_DEBUG_FUNCPTR(gst_gva_roi_scale_transform_caps);
    transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_gva_roi_scale_set_caps);
    transform_class->generate_output = GST_DEBUG_FUNCPTR(gst_gva_roi_scale_generate_output);

    const auto property_flags = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(object_class, PROP_OUT_WIDTH,
                                    g_param_spec_uint("out-width", "Output Width", "Scaled ROI output width", 1,
                                                      MAX_OUT_DIMENSION, DEFAULT_OUT_WIDTH, property_flags));
    g_object_class_install_property(object_class, PROP_OUT_HEIGHT,
                                    g_param_spec_uint("out-height", "Output Height", "Scaled ROI output height", 1,
                                                      MAX_OUT_DIMENSION, DEFAULT_OUT_HEIGHT, property_flags));
    g_object_class_install_property(object_class, PROP_CONFIDENCE_THRESHOLD,
                                    g_param_spec_double("confidence-threshold", "Confidence Threshold",
                                                        "Minimum detection confidence required to select an ROI", 0.0,
                                                        1.0, DEFAULT_CONFIDENCE_THRESHOLD, property_flags));
    g_object_class_install_property(
        object_class, PROP_OUTPUT_DIR,
        g_param_spec_string("output-dir", "Output Directory",
                            "If set, each scaled ROI frame is also saved as a JPEG in this directory", nullptr,
                            property_flags));
    g_object_class_install_property(
        object_class, PROP_TARGET_CLASSES,
        g_param_spec_string("target-classes", "Target Classes",
                            "Comma-separated detection labels to consider; empty means all classes", "",
                            property_flags));
}

static void gst_gva_roi_scale_init(GstGvaRoiScale *self) {
    self->out_width = DEFAULT_OUT_WIDTH;
    self->out_height = DEFAULT_OUT_HEIGHT;
    self->confidence_threshold = DEFAULT_CONFIDENCE_THRESHOLD;
    self->output_frame_number = 0;
    self->output_dir = nullptr;
    self->target_classes_csv = g_strdup("");
    self->target_classes = g_array_new(FALSE, FALSE, sizeof(GQuark));
    gst_video_info_init(&self->input_info);
    gst_video_info_init(&self->output_info);
    self->pending_input = nullptr;
    self->next_roi_index = 0;
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), FALSE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), FALSE);
}
