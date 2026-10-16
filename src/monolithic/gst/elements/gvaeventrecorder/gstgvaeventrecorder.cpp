/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvaeventrecorder.h"

#include <algorithm>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gst/analytics/analytics.h>
#include <gst/video/video.h>

#include "gva_caps.h"

#include "drop_log_throttle/drop_log_throttle.hpp"
#include "pre_event_ring/pre_event_ring.hpp"
#include "trigger_engine/trigger_engine.hpp"
#include "video_dump_writer/video_dump_writer.hpp"

GST_DEBUG_CATEGORY(gst_gvaeventrecorder_debug);
#define GST_CAT_DEFAULT gst_gvaeventrecorder_debug

using gvaeventrecorder::DetectionInput;
using gvaeventrecorder::DropLogThrottle;
using gvaeventrecorder::DumpJob;
using gvaeventrecorder::EncoderMode;
using gvaeventrecorder::PreEventRing;
using gvaeventrecorder::TriggerEngine;
using gvaeventrecorder::VideoDumpWriter;

enum {
    PROP_0,
    PROP_TARGET_CLASSES,
    PROP_CONFIDENCE_THRESHOLD,
    PROP_PRE_EVENT_FRAMES,
    PROP_POST_EVENT_FRAMES,
    PROP_EVENT_GAP_FRAMES,
    PROP_COOLDOWN_VALUE,
    PROP_MAX_CONCURRENT_EVENTS,
    PROP_FULL_CLIP_OUTPUT_DIR,
    PROP_DOWNSTREAM_OUTPUT_DIR,
    PROP_ENCODER_MODE,
    PROP_DROP_LOG_INTERVAL_MS
};

#define DEFAULT_TARGET_CLASSES "person,vehicle"
#define DEFAULT_CONFIDENCE_THRESHOLD 0.5
#define DEFAULT_PRE_EVENT_FRAMES 30
#define DEFAULT_POST_EVENT_FRAMES 30
#define DEFAULT_EVENT_GAP_FRAMES 3
#define DEFAULT_COOLDOWN_VALUE 150
#define DEFAULT_MAX_CONCURRENT_EVENTS 4
#define DEFAULT_FULL_CLIP_OUTPUT_DIR "."
#define DEFAULT_DROP_LOG_INTERVAL_MS 1500

#define GST_TYPE_GVAEVENTRECORDER_ENCODER_MODE (gst_gvaeventrecorder_encoder_mode_get_type())
static GType gst_gvaeventrecorder_encoder_mode_get_type(void) {
    static GType type = 0;
    if (g_once_init_enter(&type)) {
        static const GEnumValue values[] = {
            {GST_GVAEVENTRECORDER_ENCODER_SOFTWARE, "Software encoder (x264), keeps the GPU free", "software"},
            {GST_GVAEVENTRECORDER_ENCODER_VAAPI, "Hardware VA-API encoder (GPU media engine)", "vaapi"},
            {GST_GVAEVENTRECORDER_ENCODER_AUTO, "VA-API if available, software fallback otherwise", "auto"},
            {0, NULL, NULL}};
        GType t = g_enum_register_static("GstGvaEventRecorderEncoderMode", values);
        g_once_init_leave(&type, t);
    }
    return type;
}

static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVA_CAPS));
static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(GVA_CAPS));

G_DEFINE_TYPE(GstGvaEventRecorder, gst_gvaeventrecorder, GST_TYPE_BASE_TRANSFORM);

namespace {

struct CachedFrame {
    GstBuffer *buffer = nullptr;
    std::string annotation;
};

// One recording session currently being assembled (snapshot + event + post-event frames).
struct ActiveSession {
    std::string class_label;
    float confidence = 0.0f;
    std::vector<GstBuffer *> frames; // owning refs
    std::vector<std::string> frame_annotations;
    std::string output_path;
    std::vector<GstBuffer *> downstream_frames; // owning refs
    std::vector<std::string> downstream_annotations;
    std::string downstream_output_path;
};

// Forwarded frame held back by one frame, since only the next frame tells whether it ends the event.
struct PendingFrame {
    GstBuffer *buffer = nullptr;
    std::vector<GQuark> trigger_classes; // classes that started sessions on this frame
    std::vector<gfloat> trigger_confidences;
    bool first = false;
};

} // namespace

// Opaque per-element runtime state (C++ types), stored in GstGvaEventRecorder::priv.
struct GvaEventRecorderRuntime {
    std::unique_ptr<TriggerEngine> engine;
    PreEventRing<CachedFrame> ring;
    std::unordered_map<uint64_t, ActiveSession> active_sessions;
    DropLogThrottle drop_throttle;
    VideoDumpWriter writer;
    guint64 file_counter = 0;
    GstVideoInfo video_info;
    gboolean video_info_valid = FALSE;
    PendingFrame pending;
    std::mutex config_mutex; // guards engine/ring reconfiguration vs. transform_ip

    GvaEventRecorderRuntime()
        : ring(DEFAULT_PRE_EVENT_FRAMES, [](CachedFrame &frame) { gst_buffer_unref(frame.buffer); }),
          drop_throttle(DEFAULT_DROP_LOG_INTERVAL_MS) {
        gst_video_info_init(&video_info);
    }

    ~GvaEventRecorderRuntime() {
        if (pending.buffer)
            gst_buffer_unref(pending.buffer);
    }
};

static std::vector<std::string> parse_target_classes(const gchar *csv) {
    std::vector<std::string> result;
    if (!csv)
        return result;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        size_t start = item.find_first_not_of(" \t");
        size_t end = item.find_last_not_of(" \t");
        if (start != std::string::npos)
            result.push_back(item.substr(start, end - start + 1));
    }
    return result;
}

static std::string format_frame_annotation(const std::vector<DetectionInput> &detections,
                                           const std::vector<std::string> &target_classes, gdouble threshold) {
    std::ostringstream text;
    bool first = true;
    for (const auto &d : detections) {
        if (d.confidence < threshold ||
            std::find(target_classes.begin(), target_classes.end(), d.label) == target_classes.end())
            continue;
        if (!first)
            text << " | ";
        text << d.label << " confidence=" << std::fixed << std::setprecision(2) << d.confidence;
        first = false;
    }
    return text.str();
}

struct EventTimestamp {
    std::string filename;
    std::string display;
};

static EventTimestamp make_event_timestamp() {
    GDateTime *now = g_date_time_new_now_utc();
    gchar *filename = g_date_time_format(now, "%Y%m%dT%H%M%SZ");
    gchar *display = g_date_time_format(now, "%Y-%m-%d %H:%M:%S UTC");
    EventTimestamp timestamp{filename, display};
    g_free(filename);
    g_free(display);
    g_date_time_unref(now);
    return timestamp;
}

// Clip path: <dir>/event_<class>_<timestamp>_<counter><suffix>.mp4
static std::string build_output_path(const std::string &dir, const std::string &class_label,
                                     const std::string &timestamp, guint64 counter, const char *suffix = "") {
    gchar *name = g_strdup_printf("event_%s_%s_%06" G_GUINT64_FORMAT "%s.mp4", class_label.c_str(), timestamp.c_str(),
                                  counter, suffix);
    std::string path = dir + G_DIR_SEPARATOR_S + name;
    g_free(name);
    return path;
}

static void enqueue_dump(GstGvaEventRecorder *self, GvaEventRecorderRuntime *rt, const std::string &path,
                         std::vector<GstBuffer *> &frames, std::vector<std::string> &annotations) {
    if (frames.empty())
        return;
    DumpJob job;
    job.output_path = path;
    job.frames = std::move(frames);
    job.frame_annotations = std::move(annotations);
    job.video_info = rt->video_info;
    job.encoder_mode = static_cast<EncoderMode>(self->encoder_mode);
    rt->writer.enqueue(std::move(job));
    frames.clear();
    annotations.clear();
}

static void finish_session(GstGvaEventRecorder *self, GvaEventRecorderRuntime *rt, ActiveSession &s) {
    enqueue_dump(self, rt, s.output_path, s.frames, s.frame_annotations);
    enqueue_dump(self, rt, s.downstream_output_path, s.downstream_frames, s.downstream_annotations);
}

static TriggerEngine::Config make_engine_config(GstGvaEventRecorder *self) {
    TriggerEngine::Config cfg;
    cfg.target_classes = parse_target_classes(self->target_classes);

    cfg.confidence_threshold = static_cast<float>(self->confidence_threshold);
    cfg.cooldown_value = self->cooldown_value;
    cfg.post_event_frames = self->post_event_frames;
    cfg.event_gap_frames = self->event_gap_frames;
    cfg.max_concurrent_events = self->max_concurrent_events;
    return cfg;
}

// Re-applies properties that affect the live engine/ring to the running runtime. Safe to
// call before start() (rt == NULL -> no-op; make_engine_config() is read again in start()).
static void apply_runtime_config(GstGvaEventRecorder *self) {
    auto *rt = static_cast<GvaEventRecorderRuntime *>(self->priv);
    if (!rt)
        return;
    std::lock_guard<std::mutex> lock(rt->config_mutex);
    rt->engine->set_config(make_engine_config(self));
    rt->ring.set_capacity(self->pre_event_frames);
    rt->drop_throttle.set_interval_ms(self->drop_log_interval_ms);
}

static void gst_gvaeventrecorder_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gvaeventrecorder_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_gvaeventrecorder_finalize(GObject *object);
static gboolean gst_gvaeventrecorder_start(GstBaseTransform *base);
static gboolean gst_gvaeventrecorder_stop(GstBaseTransform *base);
static gboolean gst_gvaeventrecorder_set_caps(GstBaseTransform *base, GstCaps *incaps, GstCaps *outcaps);
static GstFlowReturn gst_gvaeventrecorder_transform_ip(GstBaseTransform *base, GstBuffer *buf);
static gboolean gst_gvaeventrecorder_sink_event(GstBaseTransform *base, GstEvent *event);

static void gst_gvaeventrecorder_class_init(GstGvaEventRecorderClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &src_template);
    gst_element_class_add_static_pad_template(element_class, &sink_template);

    gst_element_class_set_static_metadata(
        element_class, "Event Recorder", "Video/AI",
        "Caches pre/post-event frames, triggers on detected classes with per-class cooldown, "
        "dumps triggered clips to disk, and forwards frames while an event is active",
        "Intel Corporation");

    gobject_class->set_property = gst_gvaeventrecorder_set_property;
    gobject_class->get_property = gst_gvaeventrecorder_get_property;
    gobject_class->finalize = gst_gvaeventrecorder_finalize;

    base_transform_class->start = GST_DEBUG_FUNCPTR(gst_gvaeventrecorder_start);
    base_transform_class->stop = GST_DEBUG_FUNCPTR(gst_gvaeventrecorder_stop);
    base_transform_class->set_caps = GST_DEBUG_FUNCPTR(gst_gvaeventrecorder_set_caps);
    base_transform_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gvaeventrecorder_transform_ip);
    base_transform_class->sink_event = GST_DEBUG_FUNCPTR(gst_gvaeventrecorder_sink_event);

    g_object_class_install_property(
        gobject_class, PROP_TARGET_CLASSES,
        g_param_spec_string("target-classes", "Target Classes",
                            "Comma-separated detection labels that trigger a recording (e.g. \"person,vehicle\")",
                            DEFAULT_TARGET_CLASSES, G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_CONFIDENCE_THRESHOLD,
                                    g_param_spec_double("confidence-threshold", "Confidence Threshold",
                                                        "Minimum detection confidence required to trigger", 0.0, 1.0,
                                                        DEFAULT_CONFIDENCE_THRESHOLD, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_PRE_EVENT_FRAMES,
        g_param_spec_uint("pre-event-frames", "Pre-Event Frames",
                          "Number of frames preceding a trigger to keep in the shared ring buffer", 0, G_MAXUINT,
                          DEFAULT_PRE_EVENT_FRAMES, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_POST_EVENT_FRAMES,
        g_param_spec_uint("post-event-frames", "Post-Event Frames",
                          "Number of frames to keep capturing after the detected event has ended", 0, G_MAXUINT,
                          DEFAULT_POST_EVENT_FRAMES, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_EVENT_GAP_FRAMES,
        g_param_spec_uint(
            "event-gap-frames", "Event Gap Frames",
            "Consecutive frames without a matching detection tolerated before the event is considered ended", 0,
            G_MAXUINT, DEFAULT_EVENT_GAP_FRAMES, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_COOLDOWN_VALUE,
        g_param_spec_uint64("cooldown-value", "Cooldown Value",
                            "Per-class cooldown duration in frames, counted from the frame that started the window", 0,
                            G_MAXUINT64, DEFAULT_COOLDOWN_VALUE, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_MAX_CONCURRENT_EVENTS,
        g_param_spec_uint("max-concurrent-events", "Max Concurrent Events",
                          "Maximum number of recording sessions active at the same time; new triggers beyond this "
                          "limit are dropped (and throttle-logged)",
                          1, G_MAXUINT, DEFAULT_MAX_CONCURRENT_EVENTS, G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_FULL_CLIP_OUTPUT_DIR,
                                    g_param_spec_string("full-clip-output-dir", "Full Clip Output Directory",
                                                        "Directory where full PRE/EVENT/POST clips are written",
                                                        DEFAULT_FULL_CLIP_OUTPUT_DIR, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_DOWNSTREAM_OUTPUT_DIR,
        g_param_spec_string("downstream-output-dir", "Downstream Output Directory",
                            "Optional directory for per-event clips containing only EVENT frames forwarded downstream",
                            NULL, G_PARAM_READWRITE));

    g_object_class_install_property(gobject_class, PROP_ENCODER_MODE,
                                    g_param_spec_enum("encoder-mode", "Encoder Mode",
                                                      "Video encoder backend used to write triggered clips",
                                                      GST_TYPE_GVAEVENTRECORDER_ENCODER_MODE,
                                                      GST_GVAEVENTRECORDER_ENCODER_SOFTWARE, G_PARAM_READWRITE));

    g_object_class_install_property(
        gobject_class, PROP_DROP_LOG_INTERVAL_MS,
        g_param_spec_uint("drop-log-interval-ms", "Drop Log Interval (ms)",
                          "Minimum interval between aggregated 'events dropped' log messages", 0, G_MAXUINT,
                          DEFAULT_DROP_LOG_INTERVAL_MS, G_PARAM_READWRITE));

    GST_DEBUG_CATEGORY_INIT(gst_gvaeventrecorder_debug, "gvaeventrecorder", 0, "Event Recorder");
}

static void gst_gvaeventrecorder_init(GstGvaEventRecorder *self) {
    self->target_classes = g_strdup(DEFAULT_TARGET_CLASSES);
    self->confidence_threshold = DEFAULT_CONFIDENCE_THRESHOLD;
    self->pre_event_frames = DEFAULT_PRE_EVENT_FRAMES;
    self->post_event_frames = DEFAULT_POST_EVENT_FRAMES;
    self->event_gap_frames = DEFAULT_EVENT_GAP_FRAMES;
    self->cooldown_value = DEFAULT_COOLDOWN_VALUE;
    self->max_concurrent_events = DEFAULT_MAX_CONCURRENT_EVENTS;
    self->full_clip_output_dir = g_strdup(DEFAULT_FULL_CLIP_OUTPUT_DIR);
    self->downstream_output_dir = NULL;
    self->encoder_mode = GST_GVAEVENTRECORDER_ENCODER_SOFTWARE;
    self->drop_log_interval_ms = DEFAULT_DROP_LOG_INTERVAL_MS;
    self->frame_counter = 0;
    self->priv = NULL;
}

static void gst_gvaeventrecorder_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaEventRecorder *self = GST_GVAEVENTRECORDER(object);
    gboolean needs_reconfig = TRUE;

    switch (prop_id) {
    case PROP_TARGET_CLASSES:
        g_free(self->target_classes);
        self->target_classes = g_value_dup_string(value);
        break;
    case PROP_CONFIDENCE_THRESHOLD:
        self->confidence_threshold = g_value_get_double(value);
        break;
    case PROP_PRE_EVENT_FRAMES:
        self->pre_event_frames = g_value_get_uint(value);
        break;
    case PROP_POST_EVENT_FRAMES:
        self->post_event_frames = g_value_get_uint(value);
        break;
    case PROP_EVENT_GAP_FRAMES:
        self->event_gap_frames = g_value_get_uint(value);
        break;
    case PROP_COOLDOWN_VALUE:
        self->cooldown_value = g_value_get_uint64(value);
        break;
    case PROP_MAX_CONCURRENT_EVENTS:
        self->max_concurrent_events = g_value_get_uint(value);
        break;
    case PROP_FULL_CLIP_OUTPUT_DIR:
        g_free(self->full_clip_output_dir);
        self->full_clip_output_dir = g_value_dup_string(value);
        needs_reconfig = FALSE;
        break;
    case PROP_DOWNSTREAM_OUTPUT_DIR:
        g_free(self->downstream_output_dir);
        self->downstream_output_dir = g_value_dup_string(value);
        needs_reconfig = FALSE;
        break;
    case PROP_ENCODER_MODE:
        self->encoder_mode = g_value_get_enum(value);
        needs_reconfig = FALSE;
        break;
    case PROP_DROP_LOG_INTERVAL_MS:
        self->drop_log_interval_ms = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        return;
    }

    if (needs_reconfig)
        apply_runtime_config(self);
}

static void gst_gvaeventrecorder_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaEventRecorder *self = GST_GVAEVENTRECORDER(object);

    switch (prop_id) {
    case PROP_TARGET_CLASSES:
        g_value_set_string(value, self->target_classes);
        break;
    case PROP_CONFIDENCE_THRESHOLD:
        g_value_set_double(value, self->confidence_threshold);
        break;
    case PROP_PRE_EVENT_FRAMES:
        g_value_set_uint(value, self->pre_event_frames);
        break;
    case PROP_POST_EVENT_FRAMES:
        g_value_set_uint(value, self->post_event_frames);
        break;
    case PROP_EVENT_GAP_FRAMES:
        g_value_set_uint(value, self->event_gap_frames);
        break;
    case PROP_COOLDOWN_VALUE:
        g_value_set_uint64(value, self->cooldown_value);
        break;
    case PROP_MAX_CONCURRENT_EVENTS:
        g_value_set_uint(value, self->max_concurrent_events);
        break;
    case PROP_FULL_CLIP_OUTPUT_DIR:
        g_value_set_string(value, self->full_clip_output_dir);
        break;
    case PROP_DOWNSTREAM_OUTPUT_DIR:
        g_value_set_string(value, self->downstream_output_dir);
        break;
    case PROP_ENCODER_MODE:
        g_value_set_enum(value, self->encoder_mode);
        break;
    case PROP_DROP_LOG_INTERVAL_MS:
        g_value_set_uint(value, self->drop_log_interval_ms);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gvaeventrecorder_finalize(GObject *object) {
    GstGvaEventRecorder *self = GST_GVAEVENTRECORDER(object);

    g_free(self->target_classes);
    g_free(self->full_clip_output_dir);
    g_free(self->downstream_output_dir);

    if (self->priv) {
        delete static_cast<GvaEventRecorderRuntime *>(self->priv);
        self->priv = NULL;
    }

    G_OBJECT_CLASS(gst_gvaeventrecorder_parent_class)->finalize(object);
}

static gboolean gst_gvaeventrecorder_start(GstBaseTransform *base) {
    GstGvaEventRecorder *self = GST_GVAEVENTRECORDER(base);

    auto *rt = new GvaEventRecorderRuntime();
    rt->engine = std::make_unique<TriggerEngine>(make_engine_config(self));
    rt->ring.set_capacity(self->pre_event_frames);
    rt->drop_throttle.set_interval_ms(self->drop_log_interval_ms);
    rt->writer.start();
    self->priv = rt;

    GST_INFO_OBJECT(self,
                    "Started: target-classes=%s confidence-threshold=%.2f pre-event-frames=%u "
                    "post-event-frames=%u max-concurrent-events=%u",
                    self->target_classes, self->confidence_threshold, self->pre_event_frames, self->post_event_frames,
                    self->max_concurrent_events);
    return TRUE;
}

static gboolean gst_gvaeventrecorder_stop(GstBaseTransform *base) {
    GstGvaEventRecorder *self = GST_GVAEVENTRECORDER(base);
    auto *rt = static_cast<GvaEventRecorderRuntime *>(self->priv);
    if (!rt)
        return TRUE;

    // Finish and flush whatever each still-active session has collected so far, rather than
    // discarding partially recorded clips when the pipeline stops.
    for (auto &kv : rt->active_sessions)
        finish_session(self, rt, kv.second);
    rt->active_sessions.clear();

    rt->writer.stop(); // drains the queue before returning

    if (guint32 n = rt->drop_throttle.force_flush())
        GST_WARNING_OBJECT(self, "%u event(s) dropped due to max-concurrent-events limit (final flush)", n);

    delete rt;
    self->priv = NULL;
    return TRUE;
}

static gboolean gst_gvaeventrecorder_set_caps(GstBaseTransform *base, GstCaps *incaps, GstCaps * /*outcaps*/) {
    GstGvaEventRecorder *self = GST_GVAEVENTRECORDER(base);
    auto *rt = static_cast<GvaEventRecorderRuntime *>(self->priv);

    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, incaps)) {
        GST_ELEMENT_ERROR(self, STREAM, FORMAT, ("Failed to parse input caps"),
                          ("Could not extract video information from input capabilities"));
        return FALSE;
    }

    if (rt) {
        rt->video_info = info;
        rt->video_info_valid = TRUE;
    }
    return TRUE;
}

// Reads all object-detection metadata attached to @buf (as produced by e.g. gvadetect).
static std::vector<DetectionInput> collect_detections(GstBuffer *buf) {
    std::vector<DetectionInput> detections;
    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(buf);
    if (!rmeta)
        return detections;

    gpointer state = nullptr;
    GstAnalyticsODMtd od;
    while (gst_analytics_relation_meta_iterate(rmeta, &state, gst_analytics_od_mtd_get_mtd_type(), &od)) {
        gint x = 0, y = 0, w = 0, h = 0;
        gfloat confidence = 0.f;
        if (!gst_analytics_od_mtd_get_location(&od, &x, &y, &w, &h, &confidence))
            continue;
        GQuark label_quark = gst_analytics_od_mtd_get_obj_type(&od);
        const gchar *label = g_quark_to_string(label_quark);
        if (label)
            detections.push_back({label, confidence});
    }
    return detections;
}

// Takes ownership of frame.buffer.
static GstFlowReturn push_event_frame(GstGvaEventRecorder *self, PendingFrame frame, const char *state) {
    GstBuffer *buf = gst_buffer_make_writable(frame.buffer);
    if (frame.first)
        GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_DISCONT);

    GstAnalyticsRelationMeta *rmeta = gst_buffer_get_analytics_relation_meta(buf);
    if (!rmeta)
        rmeta = gst_buffer_add_analytics_relation_meta(buf); // gap frames may carry no detections

    // Frame-level ClsMtd (no relation to any ODMtd), identified by semantic tag.
    GstAnalyticsClsMtd state_mtd;
    if (rmeta &&
        gst_analytics_relation_meta_add_one_cls_mtd(rmeta, 1.0f, g_quark_from_static_string(state), &state_mtd))
        gst_analytics_mtd_set_semantic_tag(reinterpret_cast<GstAnalyticsMtd *>(&state_mtd), "event");
    else
        GST_WARNING_OBJECT(self, "Failed to add event state metadata");

    GstAnalyticsClsMtd trigger_mtd;
    if (rmeta && !frame.trigger_classes.empty()) {
        if (gst_analytics_relation_meta_add_cls_mtd(rmeta, frame.trigger_classes.size(),
                                                    frame.trigger_confidences.data(), frame.trigger_classes.data(),
                                                    &trigger_mtd))
            gst_analytics_mtd_set_semantic_tag(reinterpret_cast<GstAnalyticsMtd *>(&trigger_mtd), "event_trigger");
        else
            GST_WARNING_OBJECT(self, "Failed to add event trigger metadata");
    }

    return gst_pad_push(GST_BASE_TRANSFORM_SRC_PAD(self), buf);
}

static gboolean gst_gvaeventrecorder_sink_event(GstBaseTransform *base, GstEvent *event) {
    GstGvaEventRecorder *self = GST_GVAEVENTRECORDER(base);
    auto *rt = static_cast<GvaEventRecorderRuntime *>(self->priv);

    if (rt && (GST_EVENT_TYPE(event) == GST_EVENT_EOS || GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_STOP)) {
        PendingFrame pending;
        {
            std::lock_guard<std::mutex> lock(rt->config_mutex);
            pending = std::exchange(rt->pending, PendingFrame{});
        }
        if (pending.buffer) {
            if (GST_EVENT_TYPE(event) == GST_EVENT_EOS)
                push_event_frame(self, std::move(pending), "stop");
            else
                gst_buffer_unref(pending.buffer);
        }
    }

    return GST_BASE_TRANSFORM_CLASS(gst_gvaeventrecorder_parent_class)->sink_event(base, event);
}

static GstFlowReturn gst_gvaeventrecorder_transform_ip(GstBaseTransform *base, GstBuffer *buf) {
    GstGvaEventRecorder *self = GST_GVAEVENTRECORDER(base);
    auto *rt = static_cast<GvaEventRecorderRuntime *>(self->priv);
    if (!rt) {
        GST_ELEMENT_ERROR(self, CORE, STATE_CHANGE, ("Element not started"), (NULL));
        return GST_FLOW_ERROR;
    }
    if (!rt->video_info_valid) {
        GST_ELEMENT_ERROR(self, STREAM, FORMAT, ("No caps negotiated"),
                          ("set_caps was not called before the first buffer"));
    }

    const uint64_t tick = ++self->frame_counter;

    std::vector<DetectionInput> detections = collect_detections(buf);

    std::unique_lock<std::mutex> lock(rt->config_mutex);

    std::vector<uint64_t> active_ids_before = rt->engine->active_session_ids();

    const auto target_classes = parse_target_classes(self->target_classes);
    std::string frame_annotation = format_frame_annotation(detections, target_classes, self->confidence_threshold);
    TriggerEngine::FrameOutcome outcome = rt->engine->process_frame(tick, detections);

    EventTimestamp trigger_timestamp;
    if (!outcome.new_triggers.empty())
        trigger_timestamp = make_event_timestamp();
    if (!outcome.new_triggers.empty() && !frame_annotation.empty())
        frame_annotation += " | detected at " + trigger_timestamp.display;

    // Snapshot the ring before the current buffer is pushed, so the event frame is not duplicated.
    for (const auto &trig : outcome.new_triggers) {
        ActiveSession session;
        session.class_label = trig.class_label;
        session.confidence = trig.confidence;
        for (const CachedFrame &frame : rt->ring.snapshot()) {
            session.frames.push_back(gst_buffer_ref(frame.buffer));
            session.frame_annotations.push_back(frame.annotation);
        }
        const std::string full_clip_output_dir =
            self->full_clip_output_dir ? self->full_clip_output_dir : DEFAULT_FULL_CLIP_OUTPUT_DIR;
        session.output_path =
            build_output_path(full_clip_output_dir, trig.class_label, trigger_timestamp.filename, rt->file_counter);

        if (self->downstream_output_dir && self->downstream_output_dir[0] != '\0')
            session.downstream_output_path =
                build_output_path(self->downstream_output_dir, trig.class_label, trigger_timestamp.filename,
                                  rt->file_counter, "_downstream");
        ++rt->file_counter;
        rt->active_sessions.emplace(trig.session_id, std::move(session));
    }

    if (!outcome.new_triggers.empty())
        GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_DISCONT);

    // The held frame's state depends on whether the event continues on the current frame.
    PendingFrame to_push = std::exchange(rt->pending, PendingFrame{});
    const char *push_state = !outcome.forward ? "stop" : (to_push.first ? "start" : "continue");
    if (outcome.forward) {
        rt->pending.buffer = gst_buffer_ref(buf);
        for (const auto &trig : outcome.new_triggers) {
            rt->pending.trigger_classes.push_back(g_quark_from_string(trig.class_label.c_str()));
            rt->pending.trigger_confidences.push_back(trig.confidence);
        }
        rt->pending.first = to_push.buffer == nullptr;
    }

    for (uint64_t id : active_ids_before) {
        auto it = rt->active_sessions.find(id);
        if (it != rt->active_sessions.end()) {
            it->second.frames.push_back(gst_buffer_ref(buf));
            it->second.frame_annotations.push_back(frame_annotation);
        }
    }
    for (const auto &trig : outcome.new_triggers) {
        auto it = rt->active_sessions.find(trig.session_id);
        if (it != rt->active_sessions.end()) {
            it->second.frames.push_back(gst_buffer_ref(buf));
            it->second.frame_annotations.push_back(frame_annotation);
        }
    }

    if (self->downstream_output_dir && self->downstream_output_dir[0] != '\0') {
        for (auto &entry : rt->active_sessions) {
            if (!rt->engine->is_event_active(entry.first))
                continue;
            entry.second.downstream_frames.push_back(gst_buffer_ref(buf));
            entry.second.downstream_annotations.push_back(frame_annotation);
        }
    }

    // The pre-event ring is shared and continuous: never reset by a trigger.
    rt->ring.push({gst_buffer_ref(buf), frame_annotation});

    for (uint64_t id : outcome.finished_sessions) {
        auto it = rt->active_sessions.find(id);
        if (it == rt->active_sessions.end())
            continue;
        finish_session(self, rt, it->second);
        rt->active_sessions.erase(it);
    }

    rt->drop_throttle.record_drop(outcome.dropped_by_overflow);
    const uint64_t now_ms = static_cast<uint64_t>(g_get_monotonic_time() / 1000);
    if (guint32 n = rt->drop_throttle.maybe_flush(now_ms))
        GST_WARNING_OBJECT(self, "%u event(s) dropped due to max-concurrent-events limit since last log", n);

    lock.unlock();

    // Frames are forwarded one frame late via push_event_frame(); the current one is never passed through.
    if (to_push.buffer) {
        GstFlowReturn ret = push_event_frame(self, std::move(to_push), push_state);
        if (ret != GST_FLOW_OK)
            return ret;
    }
    return GST_BASE_TRANSFORM_FLOW_DROPPED;
}
