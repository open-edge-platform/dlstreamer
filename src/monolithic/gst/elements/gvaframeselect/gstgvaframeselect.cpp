/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvaframeselect.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <gst/analytics/analytics.h>
#include <gst/video/video.h>

#include "aggregated_roi_meta.hpp"
#include "frame_selector.hpp"
#include "gva_caps.h"

constexpr guint DEFAULT_SUB_STREAM_FRAMES = 900;
constexpr guint DEFAULT_FRAME_BUDGET = 8;
constexpr gdouble DEFAULT_CONFIDENCE_THRESHOLD = 0.5;
constexpr guint DEFAULT_ROI_PADDING = 0;
constexpr GstClockTime JPEG_ENCODE_TIMEOUT = 5 * GST_SECOND;

enum {
    PROP_0,
    PROP_SUB_STREAM_FRAMES,
    PROP_FRAME_BUDGET,
    PROP_TARGET_CLASSES,
    PROP_CONFIDENCE_THRESHOLD,
    PROP_ROI_PADDING,
    PROP_OUTPUT_DIR
};

struct GstGvaFrameSelect {
    GstBaseTransform parent;
    guint sub_stream_frames;
    guint frame_budget;
    gdouble confidence_threshold;
    guint roi_padding;
    guint frame_index;
    guint64 stream_frame_number;
    gchar *target_classes_csv;
    GArray *target_classes;
    gchar *output_dir;
    void *priv;
};

struct GstGvaFrameSelectClass {
    GstBaseTransformClass parent_class;
};

G_DEFINE_TYPE(GstGvaFrameSelect, gst_gva_frame_select, GST_TYPE_BASE_TRANSFORM)

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVA_CAPS));
static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVA_CAPS));

namespace {

struct CachedFrame {
    GstBuffer *buffer = nullptr;
    bool has_target_roi = false;
};

struct RoiBounds {
    bool valid = false;
    gint64 left = 0;
    gint64 top = 0;
    gint64 right = 0;
    gint64 bottom = 0;
};

struct GstGvaFrameSelectRuntime {
    GstVideoInfo video_info;
    gboolean video_info_valid = FALSE;
    std::vector<CachedFrame> pending_frames;
    RoiBounds aggregate_roi;

    GstGvaFrameSelectRuntime() {
        gst_video_info_init(&video_info);
    }
};

} // namespace

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

static void clear_pending_frames(GstGvaFrameSelectRuntime *runtime) {
    for (const CachedFrame &frame : runtime->pending_frames)
        gst_buffer_unref(frame.buffer);
    runtime->pending_frames.clear();
    runtime->aggregate_roi = {};
}

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
    case PROP_CONFIDENCE_THRESHOLD:
        GST_OBJECT_LOCK(self);
        self->confidence_threshold = g_value_get_double(value);
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
    case PROP_ROI_PADDING:
        GST_OBJECT_LOCK(self);
        self->roi_padding = g_value_get_uint(value);
        GST_OBJECT_UNLOCK(self);
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
    case PROP_CONFIDENCE_THRESHOLD:
        GST_OBJECT_LOCK(self);
        g_value_set_double(value, self->confidence_threshold);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_TARGET_CLASSES:
        GST_OBJECT_LOCK(self);
        g_value_set_string(value, self->target_classes_csv);
        GST_OBJECT_UNLOCK(self);
        break;
    case PROP_ROI_PADDING:
        GST_OBJECT_LOCK(self);
        g_value_set_uint(value, self->roi_padding);
        GST_OBJECT_UNLOCK(self);
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
    g_free(self->target_classes_csv);
    g_array_unref(self->target_classes);
    auto *runtime = static_cast<GstGvaFrameSelectRuntime *>(self->priv);
    if (runtime)
        clear_pending_frames(runtime);
    delete runtime;
    G_OBJECT_CLASS(gst_gva_frame_select_parent_class)->finalize(object);
}

static gboolean gst_gva_frame_select_start(GstBaseTransform *transform) {
    auto *self = reinterpret_cast<GstGvaFrameSelect *>(transform);
    self->frame_index = 0;
    self->stream_frame_number = 0;
    self->priv = new GstGvaFrameSelectRuntime();
    return TRUE;
}

static gboolean gst_gva_frame_select_stop(GstBaseTransform *transform) {
    auto *self = reinterpret_cast<GstGvaFrameSelect *>(transform);
    auto *runtime = static_cast<GstGvaFrameSelectRuntime *>(self->priv);
    if (runtime)
        clear_pending_frames(runtime);
    delete runtime;
    self->priv = nullptr;
    return TRUE;
}

static gboolean gst_gva_frame_select_set_caps(GstBaseTransform *transform, GstCaps *input_caps, GstCaps *) {
    auto *self = reinterpret_cast<GstGvaFrameSelect *>(transform);
    auto *runtime = static_cast<GstGvaFrameSelectRuntime *>(self->priv);
    if (!runtime || !gst_video_info_from_caps(&runtime->video_info, input_caps))
        return FALSE;
    runtime->video_info_valid = TRUE;
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

static bool is_target_class(GQuark label, const GArray *target_classes) {
    if (!target_classes || target_classes->len == 0)
        return false;
    const auto *labels = reinterpret_cast<const GQuark *>(target_classes->data);
    return std::find(labels, labels + target_classes->len, label) != labels + target_classes->len;
}

static bool collect_frame_rois(GstGvaFrameSelect *self, GstGvaFrameSelectRuntime *runtime, GstBuffer *buffer) {
    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
    if (!relation_meta || !runtime->video_info_valid)
        return false;

    GArray *target_classes;
    gdouble confidence_threshold;
    GST_OBJECT_LOCK(self);
    target_classes = g_array_ref(self->target_classes);
    confidence_threshold = self->confidence_threshold;
    GST_OBJECT_UNLOCK(self);

    const gint64 frame_width = GST_VIDEO_INFO_WIDTH(&runtime->video_info);
    const gint64 frame_height = GST_VIDEO_INFO_HEIGHT(&runtime->video_info);
    bool found = false;
    gpointer state = nullptr;
    GstAnalyticsODMtd detection;
    while (gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_od_mtd_get_mtd_type(), &detection)) {
        if (!is_target_class(gst_analytics_od_mtd_get_obj_type(&detection), target_classes))
            continue;

        gint x = 0, y = 0, width = 0, height = 0;
        gfloat confidence = 0.0f;
        if (!gst_analytics_od_mtd_get_location(&detection, &x, &y, &width, &height, &confidence) ||
            confidence < confidence_threshold || width <= 0 || height <= 0)
            continue;

        const gint64 left = std::max<gint64>(x, 0);
        const gint64 top = std::max<gint64>(y, 0);
        const gint64 right = std::min<gint64>(static_cast<gint64>(x) + width, frame_width);
        const gint64 bottom = std::min<gint64>(static_cast<gint64>(y) + height, frame_height);
        if (right <= left || bottom <= top)
            continue;

        RoiBounds &roi = runtime->aggregate_roi;
        if (!roi.valid) {
            roi = {true, left, top, right, bottom};
        } else {
            roi.left = std::min(roi.left, left);
            roi.top = std::min(roi.top, top);
            roi.right = std::max(roi.right, right);
            roi.bottom = std::max(roi.bottom, bottom);
        }
        found = true;
    }

    g_array_unref(target_classes);
    return found;
}

static GstFlowReturn push_pending_frames(GstGvaFrameSelect *self, GstGvaFrameSelectRuntime *runtime) {
    if (runtime->pending_frames.empty())
        return GST_FLOW_OK;
    if (!runtime->video_info_valid) {
        clear_pending_frames(runtime);
        return GST_FLOW_NOT_NEGOTIATED;
    }

    const gint64 frame_width = GST_VIDEO_INFO_WIDTH(&runtime->video_info);
    const gint64 frame_height = GST_VIDEO_INFO_HEIGHT(&runtime->video_info);
    guint padding;
    GST_OBJECT_LOCK(self);
    padding = self->roi_padding;
    GST_OBJECT_UNLOCK(self);

    RoiBounds padded_roi = runtime->aggregate_roi;
    if (padded_roi.valid) {
        padded_roi.left = std::max<gint64>(0, padded_roi.left - padding);
        padded_roi.top = std::max<gint64>(0, padded_roi.top - padding);
        padded_roi.right = std::min<gint64>(frame_width, padded_roi.right + padding);
        padded_roi.bottom = std::min<gint64>(frame_height, padded_roi.bottom + padding);
    }

    GstFlowReturn flow = GST_FLOW_OK;
    for (CachedFrame &frame : runtime->pending_frames) {
        GstBuffer *buffer = gst_buffer_make_writable(frame.buffer);
        frame.buffer = nullptr;
        if (!buffer) {
            flow = GST_FLOW_ERROR;
            continue;
        }

        const gint64 left = frame.has_target_roi && padded_roi.valid ? padded_roi.left : 0;
        const gint64 top = frame.has_target_roi && padded_roi.valid ? padded_roi.top : 0;
        const gint64 right = frame.has_target_roi && padded_roi.valid ? padded_roi.right : frame_width;
        const gint64 bottom = frame.has_target_roi && padded_roi.valid ? padded_roi.bottom : frame_height;
        if (!gst_buffer_add_video_region_of_interest_meta(
                buffer, dlstreamer::metadata::AGGREGATED_ROI_TYPE, static_cast<guint>(left),
                static_cast<guint>(top), static_cast<guint>(right - left), static_cast<guint>(bottom - top)))
            GST_WARNING_OBJECT(self, "Could not attach the aggregated ROI metadata");

        GST_OBJECT_LOCK(self);
        gchar *output_dir = g_strdup(self->output_dir);
        GST_OBJECT_UNLOCK(self);
        if (output_dir && *output_dir)
            save_jpeg(self, buffer, output_dir, GST_BUFFER_OFFSET(buffer));
        g_free(output_dir);

        if (flow == GST_FLOW_OK)
            flow = gst_pad_push(GST_BASE_TRANSFORM_SRC_PAD(self), buffer);
        else
            gst_buffer_unref(buffer);
    }

    runtime->pending_frames.clear();
    runtime->aggregate_roi = {};
    return flow;
}

static bool buffer_has_event_stop(GstBuffer *buffer) {
    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
    if (!relation_meta)
        return false;

    gpointer state = nullptr;
    GstAnalyticsClsMtd classification;
    while (gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_cls_mtd_get_mtd_type(),
                                               &classification)) {
        if (!gst_analytics_mtd_has_semantic_tag(reinterpret_cast<GstAnalyticsMtd *>(&classification), "event"))
            continue;
        for (gsize index = 0; index < gst_analytics_cls_mtd_get_length(&classification); ++index) {
            if (g_strcmp0(g_quark_to_string(gst_analytics_cls_mtd_get_quark(&classification, index)), "stop") == 0)
                return true;
        }
    }
    return false;
}

static GstFlowReturn gst_gva_frame_select_transform_ip(GstBaseTransform *transform, GstBuffer *buffer) {
    auto *self = reinterpret_cast<GstGvaFrameSelect *>(transform);
    auto *runtime = static_cast<GstGvaFrameSelectRuntime *>(self->priv);
    if (!runtime)
        return GST_FLOW_ERROR;

    const bool event_stop = buffer_has_event_stop(buffer);
    if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DISCONT)) {
        const GstFlowReturn flow = push_pending_frames(self, runtime);
        if (flow != GST_FLOW_OK)
            return flow;
        self->frame_index = 0;
    }

    // Counts every input frame from stream start; unlike frame_index it is not reset on DISCONT.
    const guint64 stream_frame_number = self->stream_frame_number++;
    const guint index = self->frame_index;
    const auto selected = gvaframeselect::select_indices(self->sub_stream_frames, self->frame_budget);
    if (++self->frame_index == self->sub_stream_frames)
        self->frame_index = 0;
    const bool selected_frame = std::binary_search(selected.begin(), selected.end(), static_cast<std::size_t>(index));

    if (selected_frame) {
        // For video, GstBuffer offset is defined as the frame number.
        GST_BUFFER_OFFSET(buffer) = stream_frame_number;
        const bool has_target_roi = collect_frame_rois(self, runtime, buffer);
        runtime->pending_frames.push_back({gst_buffer_ref(buffer), has_target_roi});
    }

    const bool block_complete = self->frame_index == 0;
    if (runtime->pending_frames.size() >= selected.size() || event_stop || block_complete) {
        const GstFlowReturn flow = push_pending_frames(self, runtime);
        if (flow != GST_FLOW_OK)
            return flow;
    }

    return GST_BASE_TRANSFORM_FLOW_DROPPED;
}

static gboolean gst_gva_frame_select_sink_event(GstBaseTransform *transform, GstEvent *event) {
    auto *self = reinterpret_cast<GstGvaFrameSelect *>(transform);
    auto *runtime = static_cast<GstGvaFrameSelectRuntime *>(self->priv);
    if (runtime && GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
        if (push_pending_frames(self, runtime) != GST_FLOW_OK)
            return FALSE;
    } else if (runtime && (GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_START ||
                           GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_STOP)) {
        clear_pending_frames(runtime);
    }
    if (GST_EVENT_TYPE(event) == GST_EVENT_STREAM_START) {
        self->stream_frame_number = 0;
        self->frame_index = 0;
    }
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
    transform_class->stop = GST_DEBUG_FUNCPTR(gst_gva_frame_select_stop);
    transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_gva_frame_select_set_caps);
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
        object_class, PROP_TARGET_CLASSES,
        g_param_spec_string("target-classes", "Target Classes",
                            "Comma-separated detection labels used when calculating the shared ROI", "person,vehicle",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, PROP_CONFIDENCE_THRESHOLD,
        g_param_spec_double("confidence-threshold", "Confidence Threshold",
                            "Minimum detection confidence used when calculating the shared ROI; set it to the same "
                            "value as gvaeventrecorder",
                            0.0, 1.0, DEFAULT_CONFIDENCE_THRESHOLD,
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        object_class, PROP_ROI_PADDING,
        g_param_spec_uint("roi-padding", "ROI Padding", "Pixels added on each side of the shared ROI", 0, G_MAXUINT,
                          DEFAULT_ROI_PADDING,
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
    self->confidence_threshold = DEFAULT_CONFIDENCE_THRESHOLD;
    self->roi_padding = DEFAULT_ROI_PADDING;
    self->frame_index = 0;
    self->stream_frame_number = 0;
    self->target_classes_csv = g_strdup("person,vehicle");
    self->target_classes = parse_target_classes(self->target_classes_csv);
    self->output_dir = nullptr;
    self->priv = nullptr;
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(self), FALSE);
}
