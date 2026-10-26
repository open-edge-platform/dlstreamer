/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvastreammux.h"
#include <gst/analytics/gstanalyticsbatchmeta.h>

#include <cstdlib>
#include <cstring>

GST_DEBUG_CATEGORY_STATIC(gst_gva_streammux_debug);
#define GST_CAT_DEFAULT gst_gva_streammux_debug

/* Properties */
enum {
    PROP_0,
    PROP_MAX_FPS,
    PROP_PTS_TOLERANCE,
    PROP_MAX_WAIT_TIME,
    PROP_MAX_QUEUE_SIZE,
    PROP_SYNC_MODE,
    PROP_OUTPUT_MODE,
};

#define DEFAULT_MAX_FPS 0.0
#define DEFAULT_PTS_TOLERANCE (20 * GST_MSECOND)
#define DEFAULT_MAX_WAIT_TIME (40 * GST_MSECOND)
#define DEFAULT_MAX_QUEUE_SIZE 2
#define DEFAULT_SYNC_MODE GVA_STREAMMUX_SYNC_MODE_NONE
#define DEFAULT_OUTPUT_MODE GVA_STREAMMUX_OUTPUT_PASSTHROUGH

GType gst_gva_streammux_sync_mode_get_type(void) {
    static gsize type_id = 0;
    if (g_once_init_enter(&type_id)) {
        static const GEnumValue values[] = {
            {GVA_STREAMMUX_SYNC_MODE_NONE, "Use buffer PTS as-is", "none"},
            {GVA_STREAMMUX_SYNC_MODE_FIRST_PTS, "Subtract each pad's first PTS", "first-pts"},
            {GVA_STREAMMUX_SYNC_MODE_SEGMENT, "Subtract each pad's segment start", "segment"},
            {GVA_STREAMMUX_SYNC_MODE_PIPELINE, "Use pipeline running time", "pipeline"},
            {GVA_STREAMMUX_SYNC_MODE_NTP, "Use GstReferenceTimestampMeta (NTP/PTP)", "ntp"},
            {0, NULL, NULL},
        };
        GType t = g_enum_register_static("GvaStreammuxSyncMode", values);
        g_once_init_leave(&type_id, t);
    }
    return (GType)type_id;
}

GType gst_gva_streammux_output_mode_get_type(void) {
    static gsize type_id = 0;
    if (g_once_init_enter(&type_id)) {
        static const GEnumValue values[] = {
            {GVA_STREAMMUX_OUTPUT_PASSTHROUGH, "Push each source buffer as-is; all sink pads must share identical caps",
             "passthrough"},
            {GVA_STREAMMUX_OUTPUT_CONTAINER, "Pack each batch into one multistream/x-analytics-batch container buffer",
             "container"},
            {0, NULL, NULL},
        };
        GType t = g_enum_register_static("GvaStreammuxOutputMode", values);
        g_once_init_leave(&type_id, t);
    }
    return (GType)type_id;
}

/* Pad templates */
#define STREAMMUX_VIDEO_CAPS                                                                                           \
    GST_VIDEO_CAPS_MAKE("{ BGRx, BGRA, BGR, NV12, I420, RGB, RGBA, RGBx }")                                            \
    "; " GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:VAMemory", "{ NV12 }") "; " GST_VIDEO_CAPS_MAKE_WITH_FEATURES(      \
        "memory:DMABuf", "{ DMA_DRM }") "; "

/* Non-video sources accepted on sink pads (e.g. lidar from g3dlidarparse,
 * processed radar). */
#define STREAMMUX_NONVIDEO_CAPS "application/x-lidar; application/x-radar-processed"

/* Output caps used in CONTAINER mode: the standard GStreamer multistream batch
 * caps (GStreamer 1.28). Each container buffer carries a GstAnalyticsBatchMeta
 * whose streams hold the per-source buffers and their original caps. */
#define STREAMMUX_BATCH_CAPS "multistream/x-analytics-batch(meta:GstAnalyticsBatchMeta)"

/* How far a pad's PTS has to jump, on a buffer already flagged DISCONT, before
 * sync-mode=first-pts treats it as the source's timeline having restarted
 * rather than a gap in an otherwise continuous one. */
#define GVA_STREAMMUX_TIMELINE_RESTART (GST_SECOND)

/* How many batches may go out with nothing delivered on a pad before it stops
 * being waited for, and how many consecutive batches may discard that pad's
 * head as stale before the same happens.
 *
 * Counted in batches rather than in milliseconds so the thresholds scale with
 * max-wait-time, which is already the user's statement of how long they are
 * willing to wait for a straggler. Erring early is cheap: a pad that stops
 * being waited for still has its buffers batched whenever they match, so the
 * cost of a false positive is a frame landing in a later batch. */
#define GVA_STREAMMUX_STALL_WINDOWS 5
#define GVA_STREAMMUX_LATE_ROUNDS 3

/* Sink accepts both video and non-video sources.
 * Note: STREAMMUX_VIDEO_CAPS already ends with "; ", so concatenate directly. */
static GstStaticPadTemplate gva_streammux_sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink_%u", GST_PAD_SINK, GST_PAD_REQUEST, GST_STATIC_CAPS(STREAMMUX_VIDEO_CAPS STREAMMUX_NONVIDEO_CAPS));

/* Src advertises, for PASSTHROUGH mode, every caps a sink pad may carry (video
 * or non-video, e.g. lidar) since passthrough copies the shared sink caps
 * verbatim; plus the batch caps for CONTAINER mode. The actual caps are fixed
 * at negotiation time.
 * Note: STREAMMUX_VIDEO_CAPS already ends with "; ", so concatenate directly. */
static GstStaticPadTemplate gva_streammux_src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                            GST_STATIC_CAPS(STREAMMUX_VIDEO_CAPS STREAMMUX_NONVIDEO_CAPS "; " STREAMMUX_BATCH_CAPS));

/* Forward declarations */
static void gst_gva_streammux_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gva_streammux_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_gva_streammux_finalize(GObject *object);
static GstPad *gst_gva_streammux_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name,
                                                 const GstCaps *caps);
static void gst_gva_streammux_release_pad(GstElement *element, GstPad *pad);
static GstStateChangeReturn gst_gva_streammux_change_state(GstElement *element, GstStateChange transition);
static GstFlowReturn gst_gva_streammux_chain(GstPad *pad, GstObject *parent, GstBuffer *buf);
static gboolean gst_gva_streammux_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);
static gboolean gst_gva_streammux_sink_query(GstPad *pad, GstObject *parent, GstQuery *query);
static gboolean gst_gva_streammux_src_query(GstPad *pad, GstObject *parent, GstQuery *query);
static gboolean gst_gva_streammux_src_event(GstPad *pad, GstObject *parent, GstEvent *event);
static void gst_gva_streammux_output_loop(gpointer user_data);

G_DEFINE_TYPE(GstGvaStreammux, gst_gva_streammux, GST_TYPE_ELEMENT);

static inline GstClockTime pts_abs_diff(GstClockTime a, GstClockTime b) {
    return (a > b) ? (a - b) : (b - a);
}

/* Look up a sink pad's muxer-owned data.
 *
 * Must be called with mux->lock held, and the result must not be kept across a
 * release of that lock: release_pad frees the data and clears the pad's
 * reference to it under the same lock, so a pointer fetched earlier can be
 * dangling by the time it is dereferenced. */
static GvaStreammuxPadData *get_pad_data(GstGvaStreammux *mux, GstPad *pad) {
    (void)mux;
    return (GvaStreammuxPadData *)g_object_get_data(G_OBJECT(pad), "mux-pad-data");
}

/* How many of the currently registered sink pads have reached EOS.
 *
 * Derived rather than counted: a counter has to be adjusted on every path that
 * adds, removes or flushes a pad, and forgetting one of them is what made the
 * muxer announce EOS downstream while live sources were still streaming. The
 * loop is over the number of sink pads, so its cost is irrelevant next to the
 * batching work it gates.
 *
 * Must be called with mux->lock held. */
static guint count_eos_pads(GstGvaStreammux *mux) {
    guint eos_pads = 0;
    for (guint i = 0; i < mux->pad_data->len; i++) {
        GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
        if (pdata && pdata->eos)
            eos_pads++;
    }
    return eos_pads;
}

static void gst_gva_streammux_class_init(GstGvaStreammuxClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_gva_streammux_debug, "gvastreammux", 0, "GVA Stream Muxer");

    gobject_class->set_property = gst_gva_streammux_set_property;
    gobject_class->get_property = gst_gva_streammux_get_property;
    gobject_class->finalize = gst_gva_streammux_finalize;

    element_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_gva_streammux_request_new_pad);
    element_class->release_pad = GST_DEBUG_FUNCPTR(gst_gva_streammux_release_pad);
    element_class->change_state = GST_DEBUG_FUNCPTR(gst_gva_streammux_change_state);

    /* Pad templates */
    gst_element_class_add_static_pad_template(element_class, &gva_streammux_src_template);
    gst_element_class_add_static_pad_template(element_class, &gva_streammux_sink_template);

    gst_element_class_set_static_metadata(
        element_class, "GVA Stream Muxer", "Muxer",
        "Muxes multiple streams with PTS-based synchronization. The 'output-mode' property selects "
        "between PASSTHROUGH (each source buffer pushed as-is; all sink pads must share identical caps) "
        "and CONTAINER (each batch packed into one multistream/x-analytics-batch buffer, e.g. video + lidar).",
        "Intel Corporation");

    /* Properties */
    g_object_class_install_property(
        gobject_class, PROP_MAX_FPS,
        g_param_spec_double("max-fps", "Max FPS",
                            "Maximum output frame rate (0 = unlimited). "
                            "Only set this when the video source is a local file. "
                            "Do not set for RTSP or live sources as it may cause pipeline stalls.",
                            0.0, G_MAXDOUBLE, DEFAULT_MAX_FPS,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_PTS_TOLERANCE,
        g_param_spec_uint64("pts-tolerance", "PTS Tolerance",
                            "Maximum PTS difference (ns) for buffers to be considered part of the same batch", 0,
                            G_MAXUINT64, DEFAULT_PTS_TOLERANCE,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_MAX_WAIT_TIME,
        g_param_spec_uint64("max-wait-time", "Max Wait Time",
                            "Maximum time (ns) to wait for other pads after first buffer in a batch arrives", 0,
                            G_MAXUINT64, DEFAULT_MAX_WAIT_TIME,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_MAX_QUEUE_SIZE,
        g_param_spec_uint("max-queue-size", "Max Queue Size",
                          "Maximum number of buffers per pad queue before blocking upstream (back-pressure)", 1,
                          G_MAXUINT, DEFAULT_MAX_QUEUE_SIZE,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SYNC_MODE,
        g_param_spec_enum("sync-mode", "Sync Mode",
                          "How to align PTS across sink pads before assembling batches. "
                          "'none' uses raw PTS (assumes upstream alignment). "
                          "'first-pts' subtracts each pad's first PTS so all pads start at 0. "
                          "'segment' subtracts each pad's GST_EVENT_SEGMENT start. "
                          "'pipeline' overwrites PTS with the pipeline running time at arrival "
                          "(useful for live multi-source where source PTS are unreliable). "
                          "'ntp' uses GstReferenceTimestampMeta on each buffer (e.g. from "
                          "rtspsrc ntp-sync=true add-reference-timestamp-meta=true) for "
                          "absolute cross-device time alignment.",
                          GST_TYPE_GVA_STREAMMUX_SYNC_MODE, DEFAULT_SYNC_MODE,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_OUTPUT_MODE,
        g_param_spec_enum("output-mode", "Output Mode",
                          "How the src pad emits each assembled batch. "
                          "'passthrough' pushes each source buffer as-is (plain video / single media type) "
                          "tagged with a single-stream GstAnalyticsBatchMeta; all sink pads must negotiate "
                          "identical caps, otherwise the element errors out. "
                          "'container' packs each batch into one multistream/x-analytics-batch buffer whose "
                          "GstAnalyticsBatchMeta carries one stream per source (supports heterogeneous "
                          "sources such as video + lidar; unpack downstream with gvastreamdemux).",
                          GST_TYPE_GVA_STREAMMUX_OUTPUT_MODE, DEFAULT_OUTPUT_MODE,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_gva_streammux_init(GstGvaStreammux *mux) {
    g_mutex_init(&mux->lock);
    g_cond_init(&mux->cond);

    /* No other thread can reach an object still under construction, but taking
     * the lock anyway keeps every access to these fields on one path, so none
     * of them has to be argued about (or annotated) individually. */
    g_mutex_lock(&mux->lock);

    mux->max_fps = DEFAULT_MAX_FPS;
    mux->pts_tolerance = DEFAULT_PTS_TOLERANCE;
    mux->max_wait_time = DEFAULT_MAX_WAIT_TIME;
    mux->max_queue_size = DEFAULT_MAX_QUEUE_SIZE;
    mux->sync_mode = DEFAULT_SYNC_MODE;
    mux->output_mode = DEFAULT_OUTPUT_MODE;

    mux->num_sink_pads = 0;
    mux->send_stream_start = TRUE;
    mux->flushing = FALSE;
    mux->flushing_pads_count = 0;
    mux->sinkpads = NULL;
    mux->current_caps = NULL;
    mux->caps_negotiated = FALSE;
    mux->events_pushed = FALSE;
    mux->caps_change_error_posted = FALSE;
    mux->anchor_regression_warned = FALSE;
    mux->segment_sent = FALSE;
    mux->segment_adopted = FALSE;
    mux->last_output_time = GST_CLOCK_TIME_NONE;
    mux->max_fps_duration = GST_CLOCK_TIME_NONE;
    mux->batch_anchor_pts = GST_CLOCK_TIME_NONE;
    mux->batch_start_real_time = 0;
    mux->last_pushed_batch_pts = GST_CLOCK_TIME_NONE;

    gst_segment_init(&mux->segment, GST_FORMAT_TIME);

    mux->pad_data = g_ptr_array_new();

    g_mutex_unlock(&mux->lock);

    /* Create source pad: adding it emits signals and takes the element's own
     * locks, so it stays outside mux->lock. */
    mux->srcpad = gst_pad_new_from_static_template(&gva_streammux_src_template, "src");
    gst_pad_set_query_function(mux->srcpad, GST_DEBUG_FUNCPTR(gst_gva_streammux_src_query));
    gst_pad_set_event_function(mux->srcpad, GST_DEBUG_FUNCPTR(gst_gva_streammux_src_event));
    gst_pad_use_fixed_caps(mux->srcpad);
    gst_element_add_pad(GST_ELEMENT(mux), mux->srcpad);
}

static GvaStreammuxQueueItem *queue_item_new(GstBuffer *buffer, GstCaps *caps) {
    GvaStreammuxQueueItem *item = g_new0(GvaStreammuxQueueItem, 1);
    item->buffer = buffer;                         /* takes ownership */
    item->caps = caps ? gst_caps_ref(caps) : NULL; /* own ref */
    return item;
}

static void queue_item_free(GvaStreammuxQueueItem *item) {
    if (!item)
        return;
    if (item->buffer)
        gst_buffer_unref(item->buffer);
    if (item->caps)
        gst_caps_unref(item->caps);
    g_free(item);
}

static void flush_pad_queue(GvaStreammuxPadData *pdata) {
    while (!g_queue_is_empty(&pdata->buffer_queue))
        queue_item_free((GvaStreammuxQueueItem *)g_queue_pop_head(&pdata->buffer_queue));
}

static void gst_gva_streammux_flush_pad_queues(GstGvaStreammux *mux) {
    for (guint i = 0; i < mux->pad_data->len; i++) {
        GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
        if (!pdata)
            continue;
        flush_pad_queue(pdata);
    }
}

static void gst_gva_streammux_finalize(GObject *object) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(object);

    /* The last reference is gone, so nothing can reach the object any more, but
     * tearing it down under the lock keeps every access to these fields on one
     * path, the way the constructor does. */
    g_mutex_lock(&mux->lock);

    gst_gva_streammux_flush_pad_queues(mux);

    for (guint i = 0; i < mux->pad_data->len; i++) {
        GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
        if (pdata) {
            g_queue_clear(&pdata->buffer_queue);
            if (pdata->caps)
                gst_caps_unref(pdata->caps);
            g_free(pdata);
        }
    }
    g_ptr_array_free(mux->pad_data, TRUE);

    /* The pads themselves are owned by GstElement, which drops them in its own
     * dispose, but that path calls gst_element_remove_pad() rather than this
     * element's release_pad, so the list nodes tracking them are left behind. */
    g_list_free(mux->sinkpads);
    mux->sinkpads = NULL;

    if (mux->current_caps) {
        gst_caps_unref(mux->current_caps);
        mux->current_caps = NULL;
    }

    g_mutex_unlock(&mux->lock);

    g_mutex_clear(&mux->lock);
    g_cond_clear(&mux->cond);

    G_OBJECT_CLASS(gst_gva_streammux_parent_class)->finalize(object);
}

/* Property set/get */
/* Properties can be set at any time, from a thread that is not the one
 * batching or answering queries, so every assignment here is made under the
 * lock and every reader either holds it or takes a snapshot under it. */
static void gst_gva_streammux_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(object);
    gboolean latency_changed = FALSE;

    /* Properties can be set while the element is running, so the streaming
     * threads that read them under the lock have to see one consistent value. */
    g_mutex_lock(&mux->lock);

    switch (prop_id) {
    case PROP_MAX_FPS:
        mux->max_fps = g_value_get_double(value);
        if (mux->max_fps > 0.0) {
            mux->max_fps_duration = (GstClockTime)(GST_SECOND / mux->max_fps);
        } else {
            mux->max_fps_duration = GST_CLOCK_TIME_NONE;
        }
        break;
    case PROP_PTS_TOLERANCE:
        mux->pts_tolerance = g_value_get_uint64(value);
        break;
    case PROP_MAX_WAIT_TIME:
        latency_changed = (mux->max_wait_time != g_value_get_uint64(value));
        mux->max_wait_time = g_value_get_uint64(value);
        break;
    case PROP_MAX_QUEUE_SIZE:
        latency_changed = (mux->max_queue_size != g_value_get_uint(value));
        mux->max_queue_size = g_value_get_uint(value);
        break;
    case PROP_SYNC_MODE:
        mux->sync_mode = (GvaStreammuxSyncMode)g_value_get_enum(value);
        break;
    case PROP_OUTPUT_MODE:
        mux->output_mode = (GvaStreammuxOutputMode)g_value_get_enum(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }

    g_mutex_unlock(&mux->lock);

    /* max-wait-time and max-queue-size are both part of the latency this
     * element reports, so changing either one invalidates what the pipeline
     * distributed to its sinks. Ask for it to be recalculated; posting the
     * message outside the lock, since it reaches the bus and from there
     * arbitrary application code. */
    if (latency_changed)
        gst_element_post_message(GST_ELEMENT(mux), gst_message_new_latency(GST_OBJECT(mux)));
}

static void gst_gva_streammux_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(object);

    g_mutex_lock(&mux->lock);

    switch (prop_id) {
    case PROP_MAX_FPS:
        g_value_set_double(value, mux->max_fps);
        break;
    case PROP_PTS_TOLERANCE:
        g_value_set_uint64(value, mux->pts_tolerance);
        break;
    case PROP_MAX_WAIT_TIME:
        g_value_set_uint64(value, mux->max_wait_time);
        break;
    case PROP_MAX_QUEUE_SIZE:
        g_value_set_uint(value, mux->max_queue_size);
        break;
    case PROP_SYNC_MODE:
        g_value_set_enum(value, mux->sync_mode);
        break;
    case PROP_OUTPUT_MODE:
        g_value_set_enum(value, mux->output_mode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }

    g_mutex_unlock(&mux->lock);
}

/* Normalize buffer PTS based on sync-mode. Caller must ensure buf is writable. */
static void normalize_buffer_pts(GstGvaStreammux *mux, GvaStreammuxPadData *pdata, GstBuffer *buf) {
    GstClockTime raw_pts = GST_BUFFER_PTS(buf);

    switch (mux->sync_mode) {
    case GVA_STREAMMUX_SYNC_MODE_FIRST_PTS: {
        if (!GST_CLOCK_TIME_IS_VALID(raw_pts))
            return;

        if (!pdata->first_pts_set) {
            pdata->pts_offset = (gint64)raw_pts;
            pdata->first_pts_set = TRUE;
            pdata->last_norm_pts = 0;
            GST_INFO_OBJECT(mux, "Pad sink_%u starts at %" GST_TIME_FORMAT, pdata->pad_index, GST_TIME_ARGS(raw_pts));
        } else if (GST_BUFFER_IS_DISCONT(buf) && GST_CLOCK_TIME_IS_VALID(pdata->last_raw_pts) &&
                   pts_abs_diff(raw_pts, pdata->last_raw_pts) > GVA_STREAMMUX_TIMELINE_RESTART) {
            /* The source's timeline restarted -- this is what an RTSP feed
             * looks like once it reconnects. Resume the normalized timeline
             * where it left off instead of starting it over: a pad that goes
             * back to zero while the others are seconds ahead has every buffer
             * discarded by the late-frame filter and never recovers.
             *
             * Both conditions are needed. DISCONT alone is set for an ordinary
             * dropped frame, and a gap alone can be a sparse source; together
             * they mean the timeline itself changed. The threshold is coarse on
             * purpose: inter-frame gaps and jitter are orders of magnitude
             * smaller, a reconnect is orders of magnitude larger. */
            gint64 offset = (gint64)raw_pts - (gint64)pdata->last_norm_pts;
            GST_INFO_OBJECT(mux,
                            "Pad sink_%u timeline restarted (%" GST_TIME_FORMAT " after %" GST_TIME_FORMAT
                            "); resuming normalized time from %" GST_TIME_FORMAT,
                            pdata->pad_index, GST_TIME_ARGS(raw_pts), GST_TIME_ARGS(pdata->last_raw_pts),
                            GST_TIME_ARGS(pdata->last_norm_pts));
            pdata->pts_offset = offset;
        }

        pdata->last_raw_pts = raw_pts;
        gint64 normalized = (gint64)raw_pts - pdata->pts_offset;
        if (normalized < 0)
            normalized = 0;
        pdata->last_norm_pts = (GstClockTime)normalized;
        GST_BUFFER_PTS(buf) = (GstClockTime)normalized;
        break;
    }

    case GVA_STREAMMUX_SYNC_MODE_SEGMENT:
        if (!GST_CLOCK_TIME_IS_VALID(raw_pts) || !GST_CLOCK_TIME_IS_VALID(pdata->segment_start))
            return;
        if (raw_pts >= pdata->segment_start)
            GST_BUFFER_PTS(buf) = raw_pts - pdata->segment_start;
        break;

    case GVA_STREAMMUX_SYNC_MODE_PIPELINE: {
        GstClock *clock = gst_element_get_clock(GST_ELEMENT(mux));
        if (!clock) {
            GST_LOG_OBJECT(mux,
                           "sync-mode=pipeline: no clock selected yet on sink_%u "
                           "(pipeline only picks a clock when at least one source is live)",
                           pdata->pad_index);
            return;
        }
        GstClockTime now = gst_clock_get_time(clock);
        GstClockTime base = gst_element_get_base_time(GST_ELEMENT(mux));
        gst_object_unref(clock);
        if (GST_CLOCK_TIME_IS_VALID(now) && GST_CLOCK_TIME_IS_VALID(base) && now >= base)
            GST_BUFFER_PTS(buf) = now - base;
        break;
    }

    case GVA_STREAMMUX_SYNC_MODE_NTP: {
        GstReferenceTimestampMeta *ref = gst_buffer_get_reference_timestamp_meta(buf, NULL);
        if (ref) {
            GST_BUFFER_PTS(buf) = ref->timestamp;
        } else {
            GST_LOG_OBJECT(mux,
                           "sync-mode=ntp but no GstReferenceTimestampMeta on sink_%u; "
                           "ensure upstream is e.g. rtspsrc ntp-sync=true add-reference-timestamp-meta=true",
                           pdata->pad_index);
        }
        break;
    }

    case GVA_STREAMMUX_SYNC_MODE_NONE:
    default:
        break;
    }
}

/* Request pad creation */
static GstPad *gst_gva_streammux_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *req_name,
                                                 const GstCaps *caps) {
    (void)caps;
    (void)templ;
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(element);
    GstPad *sinkpad;
    gchar *name;
    guint pad_index;
    guint64 parsed_index = 0;

    g_mutex_lock(&mux->lock);

    if (req_name && g_str_has_prefix(req_name, "sink_") &&
        g_ascii_string_to_unsigned(req_name + strlen("sink_"), 10, 0, G_MAXUINT, &parsed_index, NULL)) {
        pad_index = (guint)parsed_index;
        name = g_strdup(req_name);
    } else {
        /* First free slot, not the pad count: with sink_0..sink_2 created and
         * sink_1 released the count is 2, which would name the new pad sink_2
         * on top of the existing one. */
        pad_index = 0;
        while (pad_index < mux->pad_data->len && g_ptr_array_index(mux->pad_data, pad_index) != NULL)
            pad_index++;
        name = g_strdup_printf("sink_%u", pad_index);
    }

    if (pad_index >= GST_GVA_STREAMMUX_MAX_PAD_INDEX) {
        GST_ERROR_OBJECT(mux, "Pad index %u exceeds maximum (%u). Use sink_0 to sink_%u.", pad_index,
                         GST_GVA_STREAMMUX_MAX_PAD_INDEX, GST_GVA_STREAMMUX_MAX_PAD_INDEX - 1);
        g_free(name);
        g_mutex_unlock(&mux->lock);
        return NULL;
    }

    if (pad_index < mux->pad_data->len && g_ptr_array_index(mux->pad_data, pad_index) != NULL) {
        GST_ERROR_OBJECT(mux, "Pad index %u is already in use by %s", pad_index, name);
        g_free(name);
        g_mutex_unlock(&mux->lock);
        return NULL;
    }

    sinkpad = gst_pad_new_from_static_template(&gva_streammux_sink_template, name);
    gst_pad_set_chain_function(sinkpad, GST_DEBUG_FUNCPTR(gst_gva_streammux_chain));
    gst_pad_set_event_function(sinkpad, GST_DEBUG_FUNCPTR(gst_gva_streammux_sink_event));
    gst_pad_set_query_function(sinkpad, GST_DEBUG_FUNCPTR(gst_gva_streammux_sink_query));

    /* Create per-pad data */
    GvaStreammuxPadData *pdata = g_new0(GvaStreammuxPadData, 1);
    pdata->pad = sinkpad;
    pdata->pad_index = pad_index;
    g_queue_init(&pdata->buffer_queue);
    pdata->eos = FALSE;
    pdata->flushing = FALSE;
    pdata->released = FALSE;
    pdata->state = GVA_STREAMMUX_PAD_ACTIVE;
    pdata->delivered = FALSE;
    pdata->idle_windows = 0;
    pdata->late_rounds = 0;
    pdata->contributed = FALSE;
    pdata->stale_dropped = FALSE;
    pdata->first_pts_set = FALSE;
    pdata->pts_offset = 0;
    pdata->last_raw_pts = GST_CLOCK_TIME_NONE;
    pdata->last_norm_pts = 0;
    pdata->segment_start = GST_CLOCK_TIME_NONE;
    pdata->caps = NULL;

    g_object_set_data(G_OBJECT(sinkpad), "mux-pad-data", pdata);

    /* Grow the array if needed */
    while (mux->pad_data->len <= pad_index)
        g_ptr_array_add(mux->pad_data, NULL);
    g_ptr_array_index(mux->pad_data, pad_index) = pdata;

    /* Deliberately no gst_pad_use_fixed_caps() here: it pins a sink pad to the
     * first caps it ever sees, which is right for PASSTHROUGH but wrong for
     * CONTAINER, where each stream carries its own caps in the batch meta and
     * may renegotiate freely. gst_gva_streammux_sink_query() applies the pin
     * only in the mode that needs it. */
    GST_PAD_SET_PROXY_ALLOCATION(sinkpad);
    if (!gst_element_add_pad(element, sinkpad)) {
        GST_ERROR_OBJECT(mux, "Failed to add pad %s", name);
        g_ptr_array_index(mux->pad_data, pad_index) = NULL;
        g_object_set_data(G_OBJECT(sinkpad), "mux-pad-data", NULL);
        g_free(pdata);
        gst_object_unref(sinkpad);
        g_free(name);
        g_mutex_unlock(&mux->lock);
        return NULL;
    }

    mux->sinkpads = g_list_append(mux->sinkpads, sinkpad);
    mux->num_sink_pads++;

    GST_INFO_OBJECT(mux, "Created sink pad %s (index=%u), total pads=%u", name, pad_index, mux->num_sink_pads);

    g_free(name);
    g_mutex_unlock(&mux->lock);

    return sinkpad;
}

static void gst_gva_streammux_release_pad(GstElement *element, GstPad *pad) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(element);

    /* Step 1: tell a chain function parked on this pad's back-pressure to give
     * up, and wake it. This has to happen before the pad is deactivated:
     * deactivation waits for the pad's stream lock, which that chain function
     * holds for as long as it sits on the condition variable, so deactivating
     * first would leave the two waiting on each other. */
    g_mutex_lock(&mux->lock);
    GvaStreammuxPadData *pdata = get_pad_data(mux, pad);
    if (pdata)
        pdata->released = TRUE;
    g_cond_broadcast(&mux->cond);
    g_mutex_unlock(&mux->lock);

    /* Step 2: stop the streaming thread. Once this returns, no chain or event
     * function is running on the pad and none can be entered again, so the
     * data behind it can be torn down without racing against them. */
    gst_pad_set_active(pad, FALSE);

    /* Step 3: drop the pad's bookkeeping. */
    g_mutex_lock(&mux->lock);

    pdata = get_pad_data(mux, pad);
    if (pdata) {
        if (pdata->flushing && mux->flushing_pads_count > 0)
            mux->flushing_pads_count--;
        flush_pad_queue(pdata);
        if (pdata->pad_index < mux->pad_data->len)
            g_ptr_array_index(mux->pad_data, pdata->pad_index) = NULL;
        g_queue_clear(&pdata->buffer_queue);
        if (pdata->caps)
            gst_caps_unref(pdata->caps);
        g_free(pdata);
        g_object_set_data(G_OBJECT(pad), "mux-pad-data", NULL);
    }

    mux->sinkpads = g_list_remove(mux->sinkpads, pad);
    if (mux->num_sink_pads > 0)
        mux->num_sink_pads--;

    /* The set of eligible pads changed; wake the output loop so it re-evaluates
     * instead of waiting on a pad that no longer exists. */
    g_cond_broadcast(&mux->cond);

    GST_INFO_OBJECT(mux, "Released pad, remaining pads=%u, eos_pads=%u", mux->num_sink_pads, count_eos_pads(mux));

    g_mutex_unlock(&mux->lock);

    /* Outside the lock: removing a pad emits signals and takes the element's
     * own locks, neither of which should run underneath mux->lock. */
    gst_element_remove_pad(element, pad);
}

/* State changes */
static GstStateChangeReturn gst_gva_streammux_change_state(GstElement *element, GstStateChange transition) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(element);
    GstStateChangeReturn ret;

    switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
        break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        g_mutex_lock(&mux->lock);
        mux->send_stream_start = TRUE;
        mux->segment_sent = FALSE;
        mux->segment_adopted = FALSE;
        mux->caps_negotiated = FALSE;
        mux->events_pushed = FALSE;
        mux->flushing = FALSE;
        mux->last_output_time = GST_CLOCK_TIME_NONE;
        mux->batch_anchor_pts = GST_CLOCK_TIME_NONE;
        mux->last_pushed_batch_pts = GST_CLOCK_TIME_NONE;
        gst_segment_init(&mux->segment, GST_FORMAT_TIME);
        for (guint i = 0; i < mux->pad_data->len; i++) {
            GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
            if (pdata)
                pdata->eos = FALSE;
        }
        if (mux->pad_data->len > mux->num_sink_pads) {
            GST_WARNING_OBJECT(mux,
                               "Sparse pad indices detected: array length=%u but only %u pads created. "
                               "Consider using contiguous indices (sink_0..sink_%u) for optimal performance.",
                               mux->pad_data->len, mux->num_sink_pads, mux->num_sink_pads - 1);
        }
        g_mutex_unlock(&mux->lock);
        gst_pad_start_task(mux->srcpad, gst_gva_streammux_output_loop, mux, NULL);
        break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        /* Tear down our streaming threads BEFORE chaining up, because the
         * base-class change_state below deactivates the pads and that path
         * deadlocks against our own threads:
         *   1. chain() parks in g_cond_wait() on back-pressure while holding a
         *      sink pad's stream lock; pad deactivation needs that stream lock.
         *   2. the srcpad output-loop task holds the srcpad stream lock while
         *      running; the base class deactivating the srcpad grabs the pad
         *      object lock and waits for that stream lock, while the task waits
         *      for the object lock -> AB/BA deadlock (observed: set_state(NULL)
         *      stuck forever in change_state, "mux:src" task owning the stream
         *      lock).
         * Set flushing + broadcast so chain() and the loop return immediately,
         * then stop the srcpad task ourselves so it is fully joined (stream
         * lock released) before the base class deactivates an idle pad. */
        g_mutex_lock(&mux->lock);
        mux->flushing = TRUE;
        g_cond_broadcast(&mux->cond);
        g_mutex_unlock(&mux->lock);
        gst_pad_stop_task(mux->srcpad);
        break;
    default:
        break;
    }

    ret = GST_ELEMENT_CLASS(gst_gva_streammux_parent_class)->change_state(element, transition);

    switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        /* Streaming threads (chain + srcpad task) were already flushed and the
         * task stopped before chaining up (see the pre-transition switch), so
         * here we only reset state and drain the per-pad queues for a possible
         * restart. */
        g_mutex_lock(&mux->lock);
        gst_gva_streammux_flush_pad_queues(mux);
        mux->flushing_pads_count = 0;
        for (guint i = 0; i < mux->pad_data->len; i++) {
            GvaStreammuxPadData *pd = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
            if (pd) {
                pd->flushing = FALSE;
                pd->state = GVA_STREAMMUX_PAD_ACTIVE;
                pd->delivered = FALSE;
                pd->idle_windows = 0;
                pd->late_rounds = 0;
                pd->contributed = FALSE;
                pd->stale_dropped = FALSE;
                pd->first_pts_set = FALSE;
                pd->pts_offset = 0;
                pd->last_raw_pts = GST_CLOCK_TIME_NONE;
                pd->last_norm_pts = 0;
                pd->segment_start = GST_CLOCK_TIME_NONE;
                /* Drop cached caps so a restart re-negotiates the src caps
                 * from freshly delivered caps events. */
                if (pd->caps) {
                    gst_caps_unref(pd->caps);
                    pd->caps = NULL;
                }
            }
        }
        mux->caps_negotiated = FALSE;
        mux->events_pushed = FALSE;
        mux->caps_change_error_posted = FALSE;
        mux->anchor_regression_warned = FALSE;
        if (mux->current_caps) {
            gst_caps_unref(mux->current_caps);
            mux->current_caps = NULL;
        }
        g_mutex_unlock(&mux->lock);
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
        break;
    default:
        break;
    }

    return ret;
}

/* True once src caps can be negotiated: at least one pad exists and every pad
 * that is not EOS has reported its caps. Using "live pads" (rather than a raw
 * count vs num_sink_pads) avoids deadlocking if a pad goes EOS before ever
 * delivering caps. Must be called with mux->lock held. */
static gboolean all_live_pads_have_caps(GstGvaStreammux *mux) {
    if (mux->num_sink_pads == 0)
        return FALSE;
    guint live_with_caps = 0;
    guint live = 0;
    for (guint i = 0; i < mux->pad_data->len; i++) {
        GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
        if (!pdata || pdata->eos)
            continue;
        live++;
        if (pdata->caps)
            live_with_caps++;
    }
    return live > 0 && live_with_caps == live;
}

/* Build the source caps to push, based on the user-selected output-mode.
 * Called once all live sink pads have delivered their caps. Must be called with
 * mux->lock held.
 *
 * CONTAINER mode emits the fixed multistream batch caps. PASSTHROUGH mode
 * requires every live sink pad to have negotiated identical caps (only equality
 * is checked, not the media type); the shared caps become the src caps. A
 * mismatch is a configuration error: *caps_mismatch is set so the caller can
 * post a GST_ELEMENT_ERROR outside the lock.
 *
 * Returns the caps to push downstream (transfer full, NULL if already
 * negotiated or on mismatch), and reports via out-params whether
 * stream-start/segment must be sent first. The actual event pushing is done by
 * the caller OUTSIDE the lock to avoid deadlock with downstream queries. */
static GstCaps *negotiate_src_caps(GstGvaStreammux *mux, gboolean *need_stream_start, gboolean *need_segment,
                                   gboolean *caps_mismatch) {
    *need_stream_start = FALSE;
    *need_segment = FALSE;
    *caps_mismatch = FALSE;
    if (mux->caps_negotiated)
        return NULL;

    GstCaps *src_caps = NULL;
    if (mux->output_mode == GVA_STREAMMUX_OUTPUT_CONTAINER) {
        src_caps = gst_caps_from_string(STREAMMUX_BATCH_CAPS);
        GST_INFO_OBJECT(mux, "Output mode: CONTAINER, src caps: %" GST_PTR_FORMAT, src_caps);
    } else {
        /* PASSTHROUGH: every live sink pad must share identical caps. */
        GstCaps *shared_caps = NULL;
        for (guint i = 0; i < mux->pad_data->len; i++) {
            GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
            if (!pdata || pdata->eos || !pdata->caps)
                continue;
            if (!shared_caps) {
                shared_caps = pdata->caps;
            } else if (!gst_caps_is_equal(shared_caps, pdata->caps)) {
                GST_ERROR_OBJECT(mux,
                                 "output-mode=passthrough requires all sink pads to have identical caps, "
                                 "but sink_%u caps %" GST_PTR_FORMAT " differ from %" GST_PTR_FORMAT,
                                 pdata->pad_index, pdata->caps, shared_caps);
                *caps_mismatch = TRUE;
                return NULL;
            }
        }
        if (!shared_caps)
            return NULL; /* no live pad with caps yet */
        mux->current_caps = gst_caps_copy(shared_caps);
        src_caps = gst_caps_ref(mux->current_caps);
        GST_INFO_OBJECT(mux, "Output mode: PASSTHROUGH, src caps: %" GST_PTR_FORMAT, src_caps);
    }

    mux->caps_negotiated = TRUE;
    *need_stream_start = mux->send_stream_start;
    mux->send_stream_start = FALSE;
    *need_segment = !mux->segment_sent;
    mux->segment_sent = TRUE;
    /* Leave a segment already adopted from upstream (sync-mode=none) alone. */
    if (*need_segment && !mux->segment_adopted)
        gst_segment_init(&mux->segment, GST_FORMAT_TIME);

    return src_caps;
}

/* Sink event handler */
static gboolean gst_gva_streammux_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(parent);
    gboolean ret = TRUE;

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
        GstCaps *caps = NULL;
        gst_event_parse_caps(event, &caps);

        gboolean need_stream_start = FALSE;
        gboolean need_segment = FALSE;
        gboolean caps_mismatch = FALSE;
        GstCaps *caps_to_push = NULL;
        GstCaps *negotiated_caps = NULL;
        GstSegment segment_to_push;
        gst_segment_init(&segment_to_push, GST_FORMAT_TIME);

        g_mutex_lock(&mux->lock);
        GvaStreammuxPadData *pdata = get_pad_data(mux, pad);
        /* Copied out because it is still needed for the error messages below,
         * which are reported after the lock is dropped. */
        guint pad_index = pdata ? pdata->pad_index : 0;
        GST_INFO_OBJECT(mux, "Received caps on pad sink_%u: %" GST_PTR_FORMAT, pad_index, caps);
        if (pdata) {
            /* Store this pad's caps (replacing any previous, e.g. on
             * renegotiation). */
            if (pdata->caps)
                gst_caps_unref(pdata->caps);
            pdata->caps = gst_caps_copy(caps);
        }

        /* Defer source caps negotiation until every live sink pad has reported
         * its caps (so a passthrough caps-equality check sees all of them). */
        if (!mux->caps_negotiated && all_live_pads_have_caps(mux)) {
            caps_to_push = negotiate_src_caps(mux, &need_stream_start, &need_segment, &caps_mismatch);
            /* Copied out because the events are pushed after the lock is
             * dropped, while another pad's streaming thread may update it. */
            segment_to_push = mux->segment;
        } else if (mux->caps_negotiated && !mux->caps_change_error_posted &&
                   mux->output_mode == GVA_STREAMMUX_OUTPUT_PASSTHROUGH && mux->current_caps &&
                   !gst_caps_is_equal(mux->current_caps, caps)) {
            /* Passthrough pushes source buffers unmodified under the single src
             * caps fixed at negotiation time, so it cannot retag a stream that
             * changes format mid-flight: the buffers would keep flowing while
             * downstream still believes the old caps. Fail loudly instead.
             * CONTAINER mode has no such restriction — each buffer carries the
             * caps captured when it was queued. */
            negotiated_caps = gst_caps_ref(mux->current_caps);
            mux->caps_change_error_posted = TRUE;
        }
        g_mutex_unlock(&mux->lock);

        if (caps_mismatch) {
            GST_ELEMENT_ERROR(mux, STREAM, FORMAT, ("Sink pads have mismatched caps"),
                              ("output-mode=passthrough requires all sink pads to share identical caps; "
                               "use output-mode=container for heterogeneous sources"));
            gst_event_unref(event);
            return FALSE;
        }

        if (negotiated_caps) {
            GST_ELEMENT_ERROR(mux, STREAM, FORMAT, ("Sink pad caps changed after the source pad was negotiated"),
                              ("pad sink_%u switched to %" GST_PTR_FORMAT " but the source pad is already negotiated "
                               "as %" GST_PTR_FORMAT "; output-mode=passthrough cannot renegotiate mid-stream, "
                               "use output-mode=container for sources whose caps can change",
                               pad_index, caps, negotiated_caps));
            gst_caps_unref(negotiated_caps);
            gst_event_unref(event);
            return FALSE;
        }

        if (caps_to_push) {
            if (need_stream_start) {
                gchar *stream_id = g_strdup_printf("gvastreammux/%08x%08x", g_random_int(), g_random_int());
                gst_pad_push_event(mux->srcpad, gst_event_new_stream_start(stream_id));
                g_free(stream_id);
                GST_INFO_OBJECT(mux, "Sent stream-start event");
            }
            gst_pad_push_event(mux->srcpad, gst_event_new_caps(caps_to_push));
            GST_INFO_OBJECT(mux, "Set output caps: %" GST_PTR_FORMAT, caps_to_push);
            gst_caps_unref(caps_to_push);
            if (need_segment) {
                gst_pad_push_event(mux->srcpad, gst_event_new_segment(&segment_to_push));
                GST_INFO_OBJECT(mux, "Sent segment event");
            }
            /* The events are on the pad now, so open the output gate. */
            g_mutex_lock(&mux->lock);
            mux->events_pushed = TRUE;
            g_cond_broadcast(&mux->cond);
            g_mutex_unlock(&mux->lock);
        }

        gst_event_unref(event);
        ret = TRUE;
        break;
    }
    case GST_EVENT_STREAM_START:
        /* Consume it. The muxer presents one stream assembled from several, and
         * sends its own stream-start with its own id when it negotiates.
         * Letting the default handler forward each sink pad's as well stacks
         * them on the source pad -- four of them for two sources -- and leaves
         * downstream with whichever id happened to arrive last. */
        GST_DEBUG_OBJECT(mux, "Consuming stream-start from %s", GST_PAD_NAME(pad));
        gst_event_unref(event);
        ret = TRUE;
        break;
    case GST_EVENT_SEGMENT: {
        GstSegment seg;
        GstSegment src_segment;
        gboolean push_src_segment = FALSE;

        gst_event_copy_segment(event, &seg);
        g_mutex_lock(&mux->lock);
        GvaStreammuxPadData *pdata = get_pad_data(mux, pad);
        if (pdata) {
            pdata->segment_start = seg.start;
            GST_DEBUG_OBJECT(mux, "Pad sink_%u segment.start=%" GST_TIME_FORMAT, pdata->pad_index,
                             GST_TIME_ARGS(seg.start));
        }

        /* sync-mode=none passes every PTS through untouched, so the source pad
         * has to carry the upstream timeline as well: with a zero-based
         * segment downstream would read a buffer seeked to T as having a
         * running time of T and wait that long before rendering it. The other
         * sync modes rewrite PTS onto a zero-based timeline, which a
         * zero-based segment already describes correctly.
         *
         * The first segment of the current run wins; the rest are assumed to
         * share its timeline, which is the same assumption batching already
         * makes about the sources. */
        if (mux->sync_mode == GVA_STREAMMUX_SYNC_MODE_NONE && !mux->segment_adopted && seg.format == GST_FORMAT_TIME) {
            mux->segment = seg;
            mux->segment_adopted = TRUE;
            src_segment = mux->segment;
            /* If the events have not gone out yet, whoever sends them will
             * pick this segment up; otherwise it has to be sent as an update. */
            push_src_segment = mux->events_pushed;
            GST_DEBUG_OBJECT(mux, "Adopted segment from sink_%u as the source segment: start=%" GST_TIME_FORMAT,
                             pdata ? pdata->pad_index : 0, GST_TIME_ARGS(seg.start));
        }
        g_mutex_unlock(&mux->lock);

        if (push_src_segment)
            gst_pad_push_event(mux->srcpad, gst_event_new_segment(&src_segment));

        gst_event_unref(event);
        ret = TRUE;
        break;
    }
    case GST_EVENT_EOS: {
        gboolean need_stream_start = FALSE;
        gboolean need_segment = FALSE;
        gboolean caps_mismatch = FALSE;
        GstCaps *caps_to_push = NULL;
        GstSegment segment_to_push;
        gst_segment_init(&segment_to_push, GST_FORMAT_TIME);

        g_mutex_lock(&mux->lock);
        GvaStreammuxPadData *pdata = get_pad_data(mux, pad);
        if (pdata && !pdata->eos) {
            pdata->eos = TRUE;
            GST_INFO_OBJECT(mux, "EOS on pad sink_%u, eos_count=%u/%u", pdata->pad_index, count_eos_pads(mux),
                            mux->num_sink_pads);
        }
        /* A pad may reach EOS before ever sending caps. Once it is excluded,
         * the remaining live pads might all have caps, so (re)try negotiation
         * to avoid the output loop waiting forever for caps to be decided. */
        if (!mux->caps_negotiated && all_live_pads_have_caps(mux)) {
            caps_to_push = negotiate_src_caps(mux, &need_stream_start, &need_segment, &caps_mismatch);
            /* Copied out because the events are pushed after the lock is
             * dropped, while another pad's streaming thread may update it. */
            segment_to_push = mux->segment;
        }
        g_cond_broadcast(&mux->cond);
        g_mutex_unlock(&mux->lock);

        if (caps_mismatch) {
            GST_ELEMENT_ERROR(mux, STREAM, FORMAT, ("Sink pads have mismatched caps"),
                              ("output-mode=passthrough requires all sink pads to share identical caps; "
                               "use output-mode=container for heterogeneous sources"));
            gst_event_unref(event);
            return FALSE;
        }

        if (caps_to_push) {
            if (need_stream_start) {
                gchar *stream_id = g_strdup_printf("gvastreammux/%08x%08x", g_random_int(), g_random_int());
                gst_pad_push_event(mux->srcpad, gst_event_new_stream_start(stream_id));
                g_free(stream_id);
            }
            gst_pad_push_event(mux->srcpad, gst_event_new_caps(caps_to_push));
            gst_caps_unref(caps_to_push);
            if (need_segment)
                gst_pad_push_event(mux->srcpad, gst_event_new_segment(&segment_to_push));
            /* The events are on the pad now, so open the output gate. */
            g_mutex_lock(&mux->lock);
            mux->events_pushed = TRUE;
            g_cond_broadcast(&mux->cond);
            g_mutex_unlock(&mux->lock);
        }

        gst_event_unref(event);
        ret = TRUE;
        break;
    }
    case GST_EVENT_FLUSH_START: {
        g_mutex_lock(&mux->lock);
        GvaStreammuxPadData *pdata = get_pad_data(mux, pad);
        gboolean first_flush = FALSE;
        if (pdata && !pdata->flushing) {
            pdata->flushing = TRUE;
            mux->flushing_pads_count++;
            first_flush = (mux->flushing_pads_count == 1);
        }
        if (first_flush) {
            mux->flushing = TRUE;
            g_cond_broadcast(&mux->cond);
        }
        g_mutex_unlock(&mux->lock);

        if (first_flush) {
            /* Forward before pausing, not after. Pausing waits for the output
             * task's current iteration to finish, and that iteration may be
             * blocked inside a downstream element -- on a full queue, a
             * throttle, a slow sink. FLUSH_START is what unblocks it, so
             * holding the event back until the task has stopped means waiting
             * for the very thing the event would have released. */
            ret = gst_pad_push_event(mux->srcpad, event);
            gst_pad_pause_task(mux->srcpad);
        } else {
            gst_event_unref(event);
            ret = TRUE;
        }
        break;
    }
    case GST_EVENT_FLUSH_STOP: {
        GstSegment flushed_segment;
        gst_segment_init(&flushed_segment, GST_FORMAT_TIME);

        g_mutex_lock(&mux->lock);
        GvaStreammuxPadData *pdata = get_pad_data(mux, pad);
        gboolean last_flush = FALSE;
        if (pdata && pdata->flushing) {
            pdata->flushing = FALSE;
            if (mux->flushing_pads_count > 0)
                mux->flushing_pads_count--;
            last_flush = (mux->flushing_pads_count == 0);
        }
        if (last_flush) {
            mux->flushing = FALSE;
            gst_gva_streammux_flush_pad_queues(mux);
            for (guint i = 0; i < mux->pad_data->len; i++) {
                GvaStreammuxPadData *pd = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
                if (pd) {
                    pd->eos = FALSE;
                    pd->state = GVA_STREAMMUX_PAD_ACTIVE;
                    pd->delivered = FALSE;
                    pd->idle_windows = 0;
                    pd->late_rounds = 0;
                    pd->contributed = FALSE;
                    pd->stale_dropped = FALSE;
                    pd->first_pts_set = FALSE;
                    pd->pts_offset = 0;
                    pd->last_raw_pts = GST_CLOCK_TIME_NONE;
                    pd->last_norm_pts = 0;
                }
            }
            mux->batch_anchor_pts = GST_CLOCK_TIME_NONE;
            mux->last_pushed_batch_pts = GST_CLOCK_TIME_NONE;
            mux->segment_adopted = FALSE;
            gst_segment_init(&mux->segment, GST_FORMAT_TIME);
            flushed_segment = mux->segment;
        }
        g_mutex_unlock(&mux->lock);

        if (last_flush) {
            ret = gst_pad_push_event(mux->srcpad, event);
            /* FLUSH_STOP drops the src pad's sticky segment (it is removed
             * along with EOS in gst_pad_push_event), while stream-start and
             * caps survive. Nothing else would send a replacement -- the
             * segment is normally pushed from negotiate_src_caps(), which only
             * runs while caps are still unnegotiated -- so the next buffer
             * would hit "data flow before segment event" and downstream would
             * have no running time to schedule against. Send it here, straight
             * after the flush that removed it. */
            gst_pad_push_event(mux->srcpad, gst_event_new_segment(&flushed_segment));
            /* Only restart the task if the pad is still active. A flush that
             * races a shutdown would otherwise spawn a streaming thread on a
             * pad that has just been deactivated, and nothing would stop it
             * again: deactivation already happened. */
            if (gst_pad_is_active(mux->srcpad))
                gst_pad_start_task(mux->srcpad, gst_gva_streammux_output_loop, mux, NULL);
            else
                GST_DEBUG_OBJECT(mux, "Source pad is no longer active; not restarting the output task");
        } else {
            gst_event_unref(event);
            ret = TRUE;
        }
        break;
    }
    default:
        ret = gst_pad_event_default(pad, parent, event);
        break;
    }

    return ret;
}

/* Chain function: receives buffers from upstream */
static GstFlowReturn gst_gva_streammux_chain(GstPad *pad, GstObject *parent, GstBuffer *buf) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(parent);

    /* One snapshot drives both decisions below, so a concurrent property change
     * cannot leave normalize_buffer_pts writing into a buffer that was not made
     * writable. */
    g_mutex_lock(&mux->lock);
    GvaStreammuxSyncMode sync_mode = mux->sync_mode;
    g_mutex_unlock(&mux->lock);

    /* Making the buffer writable does not touch the pad data, so it can stay
     * outside the lock; normalize_buffer_pts reads and writes that data and so
     * has to move inside it. */
    if (sync_mode != GVA_STREAMMUX_SYNC_MODE_NONE)
        buf = gst_buffer_make_writable(buf);

    g_mutex_lock(&mux->lock);

    GvaStreammuxPadData *pdata = get_pad_data(mux, pad);
    if (!pdata) {
        g_mutex_unlock(&mux->lock);
        GST_ERROR_OBJECT(mux, "No pad data for pad %s", GST_PAD_NAME(pad));
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    if (mux->flushing) {
        g_mutex_unlock(&mux->lock);
        gst_buffer_unref(buf);
        return GST_FLOW_FLUSHING;
    }

    if (sync_mode != GVA_STREAMMUX_SYNC_MODE_NONE)
        normalize_buffer_pts(mux, pdata, buf);

    /* Back-pressure: block if queue is full.
     *
     * The lock is dropped while waiting, so the pad data has to be looked up
     * again on every wake-up: release_pad can free it in the meantime, and
     * this loop would otherwise go straight back to reading the freed queue. */
    while (g_queue_get_length(&pdata->buffer_queue) >= mux->max_queue_size && !mux->flushing && !pdata->released) {
        g_cond_wait(&mux->cond, &mux->lock);
        pdata = get_pad_data(mux, pad);
        if (!pdata) {
            g_mutex_unlock(&mux->lock);
            GST_DEBUG_OBJECT(mux, "Pad %s was released while waiting for queue space", GST_PAD_NAME(pad));
            gst_buffer_unref(buf);
            return GST_FLOW_FLUSHING;
        }
    }

    if (mux->flushing || pdata->released) {
        g_mutex_unlock(&mux->lock);
        gst_buffer_unref(buf);
        return GST_FLOW_FLUSHING;
    }

    /* Late buffers are not discarded here. The batching loop discards the
     * heads it cannot use, which covers strictly more than this did -- it has
     * no tolerance window -- and does it at the point where staleness
     * actually decides something.
     *
     * Dropping here was also wrong in a way the loop is not: it returned
     * before the pad was marked as having delivered, so a source whose
     * buffers were all late never counted as alive. After five batches it was
     * called stalled, and since recovery is judged on delivery it could never
     * stop being called that -- while it was delivering the whole time.
     *
     * Capture the pad's current caps alongside the buffer: CAPS events and
     * buffers are serialized on this streaming thread, so pdata->caps is
     * exactly what upstream negotiated for this buffer. Reading it back when
     * the batch is assembled would instead tag already-queued buffers with the
     * caps of a later, mid-stream caps change. */
    g_queue_push_tail(&pdata->buffer_queue, queue_item_new(buf, pdata->caps));
    pdata->delivered = TRUE;
    /* Broadcast, not signal. Two kinds of thread wait on this one condition
     * variable: the output task, waiting for a batch to become possible, and
     * other chain functions, waiting for room in their own queues. Waking
     * exactly one of them can wake a chain function whose queue is still
     * full, which simply waits again -- and the output task, the only thread
     * that drains any queue, is left asleep with nothing to wake it. */
    g_cond_broadcast(&mux->cond);

    g_mutex_unlock(&mux->lock);
    return GST_FLOW_OK;
}

/* Returns the caps a sink pad may currently accept (transfer full).
 *
 * PASSTHROUGH forwards buffers unmodified under the single src caps fixed at
 * negotiation time, so once negotiated a sink pad is pinned to exactly those
 * caps. CONTAINER gives every stream its own caps in the batch meta, so a sink
 * pad stays open to anything the template allows and may renegotiate at will.
 * Must be called with mux->lock held. */
static GstCaps *sink_pad_allowed_caps(GstGvaStreammux *mux, GstPad *pad) {
    if (mux->output_mode == GVA_STREAMMUX_OUTPUT_PASSTHROUGH && mux->caps_negotiated && mux->current_caps)
        return gst_caps_ref(mux->current_caps);
    return gst_pad_get_pad_template_caps(pad);
}

/* Sink pad query handler.
 *
 * This stands in for gst_pad_use_fixed_caps(), which would pin every sink pad
 * to the first caps it ever saw and so make CONTAINER mode reject the
 * per-stream renegotiation it is meant to support. */
static gboolean gst_gva_streammux_sink_query(GstPad *pad, GstObject *parent, GstQuery *query) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(parent);

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_CAPS: {
        GstCaps *filter = NULL;
        gst_query_parse_caps(query, &filter);

        g_mutex_lock(&mux->lock);
        GstCaps *allowed = sink_pad_allowed_caps(mux, pad);
        g_mutex_unlock(&mux->lock);

        GstCaps *result = allowed;
        if (filter) {
            result = gst_caps_intersect_full(filter, allowed, GST_CAPS_INTERSECT_FIRST);
            gst_caps_unref(allowed);
        }
        gst_query_set_caps_result(query, result);
        gst_caps_unref(result);
        return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS: {
        GstCaps *caps = NULL;
        gst_query_parse_accept_caps(query, &caps);

        g_mutex_lock(&mux->lock);
        GstCaps *allowed = sink_pad_allowed_caps(mux, pad);
        g_mutex_unlock(&mux->lock);

        gboolean accepted = gst_caps_is_subset(caps, allowed);
        if (!accepted)
            GST_INFO_OBJECT(mux, "Rejecting caps %" GST_PTR_FORMAT " on %s, allowed: %" GST_PTR_FORMAT, caps,
                            GST_PAD_NAME(pad), allowed);
        gst_caps_unref(allowed);

        gst_query_set_accept_caps_result(query, accepted);
        return TRUE;
    }
    default:
        return gst_pad_query_default(pad, parent, query);
    }
}

/* Worst-case time a buffer can sit in a sink pad's queue before it is batched,
 * or GST_CLOCK_TIME_NONE when that cannot be worked out.
 *
 * The queues are bounded in buffers, not in time, so the answer needs a frame
 * duration: the negotiated caps supply one in PASSTHROUGH mode, where every
 * pad carries the same framerate. CONTAINER mode deliberately allows the pads
 * to disagree -- that is the point of it -- so there is no single duration to
 * use and nothing is claimed. Must be called with mux->lock held. */
static GstClockTime queueing_latency(GstGvaStreammux *mux) {
    if (!mux->current_caps || mux->max_queue_size == 0)
        return GST_CLOCK_TIME_NONE;

    const GstStructure *st = gst_caps_get_structure(mux->current_caps, 0);
    gint fps_n = 0, fps_d = 0;
    if (!st || !gst_structure_get_fraction(st, "framerate", &fps_n, &fps_d) || fps_n <= 0 || fps_d <= 0)
        return GST_CLOCK_TIME_NONE;

    return gst_util_uint64_scale_int(mux->max_queue_size * GST_SECOND, fps_d, fps_n);
}

/* Source pad query handler */
static gboolean gst_gva_streammux_src_query(GstPad *pad, GstObject *parent, GstQuery *query) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(parent);

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_LATENCY: {
        gboolean live = FALSE;
        GstClockTime min_latency = 0, max_latency = GST_CLOCK_TIME_NONE;

        GList *pads_copy = NULL;
        GList *l;

        g_mutex_lock(&mux->lock);
        for (l = mux->sinkpads; l; l = l->next)
            pads_copy = g_list_prepend(pads_copy, gst_object_ref(GST_PAD(l->data)));
        GstClockTime max_wait_time = mux->max_wait_time;
        GstClockTime queueing = queueing_latency(mux);
        g_mutex_unlock(&mux->lock);

        for (l = pads_copy; l; l = l->next) {
            GstPad *sinkpad = GST_PAD(l->data);
            GstQuery *peer_query = gst_query_new_latency();
            if (gst_pad_peer_query(sinkpad, peer_query)) {
                gboolean peer_live;
                GstClockTime peer_min, peer_max;
                gst_query_parse_latency(peer_query, &peer_live, &peer_min, &peer_max);
                live = live || peer_live;
                min_latency = MAX(min_latency, peer_min);
                if (GST_CLOCK_TIME_IS_VALID(peer_max)) {
                    if (GST_CLOCK_TIME_IS_VALID(max_latency))
                        max_latency = MAX(max_latency, peer_max);
                    else
                        max_latency = peer_max;
                }
            }
            gst_query_unref(peer_query);
        }
        g_list_free_full(pads_copy, gst_object_unref);

        /* Every batch can be held for up to max-wait-time, whether or not the
         * queues are busy, so that goes on both bounds. The queues only delay
         * a buffer when they are backed up, which is a worst case, so that
         * goes on the upper bound alone. */
        min_latency += max_wait_time;
        if (GST_CLOCK_TIME_IS_VALID(max_latency)) {
            max_latency += max_wait_time;
            if (GST_CLOCK_TIME_IS_VALID(queueing))
                max_latency += queueing;
        }

        GST_DEBUG_OBJECT(mux, "Reporting latency: live=%d min=%" GST_TIME_FORMAT " max=%" GST_TIME_FORMAT, live,
                         GST_TIME_ARGS(min_latency), GST_TIME_ARGS(max_latency));

        gst_query_set_latency(query, live, min_latency, max_latency);
        return TRUE;
    }
    case GST_QUERY_CAPS: {
        GstCaps *filter;
        gst_query_parse_caps(query, &filter);
        GstCaps *caps = gst_pad_get_pad_template_caps(pad);

        /* This query arrives on whatever thread downstream happens to use,
         * while a flush or a state change can be dropping the last reference
         * to current_caps. Take a reference under the lock and work on that. */
        g_mutex_lock(&mux->lock);
        GstCaps *negotiated = mux->current_caps ? gst_caps_ref(mux->current_caps) : NULL;
        g_mutex_unlock(&mux->lock);

        if (negotiated) {
            GstCaps *result = gst_caps_intersect(caps, negotiated);
            gst_caps_unref(negotiated);
            gst_caps_unref(caps);
            caps = result;
        }
        if (filter) {
            GstCaps *result = gst_caps_intersect(caps, filter);
            gst_caps_unref(caps);
            caps = result;
        }
        gst_query_set_caps_result(query, caps);
        gst_caps_unref(caps);
        return TRUE;
    }
    default:
        return gst_pad_query_default(pad, parent, query);
    }
}

/* Source pad event handler */
static gboolean gst_gva_streammux_src_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(parent);

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_QOS:
    case GST_EVENT_SEEK: {
        gboolean ret = TRUE;
        GList *pads_copy = NULL;
        GList *l;

        g_mutex_lock(&mux->lock);
        for (l = mux->sinkpads; l; l = l->next)
            pads_copy = g_list_prepend(pads_copy, gst_object_ref(GST_PAD(l->data)));
        g_mutex_unlock(&mux->lock);

        for (l = pads_copy; l; l = l->next) {
            GstPad *sinkpad = GST_PAD(l->data);
            gst_event_ref(event);
            if (!gst_pad_push_event(sinkpad, event))
                ret = FALSE;
        }
        g_list_free_full(pads_copy, gst_object_unref);
        gst_event_unref(event);
        return ret;
    }
    default:
        return gst_pad_event_default(pad, parent, event);
    }
}

/* Apply max-fps throttling. Called with the lock released, since it sleeps. */
static void gst_gva_streammux_apply_fps_throttle(GstGvaStreammux *mux) {
    g_mutex_lock(&mux->lock);
    GstClockTime max_fps_duration = mux->max_fps_duration;
    GstClockTime last_output_time = mux->last_output_time;
    g_mutex_unlock(&mux->lock);

    if (!GST_CLOCK_TIME_IS_VALID(max_fps_duration))
        return;

    GstClock *clock = gst_element_get_clock(GST_ELEMENT(mux));
    if (!clock)
        return;

    GstClockTime now = gst_clock_get_time(clock);
    gst_object_unref(clock);

    if (GST_CLOCK_TIME_IS_VALID(last_output_time)) {
        GstClockTime elapsed = now - last_output_time;
        if (elapsed < max_fps_duration) {
            GstClockTime wait = max_fps_duration - elapsed;
            GST_LOG_OBJECT(mux, "FPS throttle: waiting %" GST_TIME_FORMAT, GST_TIME_ARGS(wait));
            g_usleep(GST_TIME_AS_USECONDS(wait));
        }
    }
}

/* Called with the lock released, like the throttle that reads this back. */
static void gst_gva_streammux_update_output_time(GstGvaStreammux *mux) {
    GstClock *clock = gst_element_get_clock(GST_ELEMENT(mux));
    if (!clock)
        return;

    GstClockTime now = gst_clock_get_time(clock);
    gst_object_unref(clock);

    g_mutex_lock(&mux->lock);
    mux->last_output_time = now;
    g_mutex_unlock(&mux->lock);
}

/* Drop buffers at the head of a pad's queue that are further behind the last
 * batch than pts-tolerance allows, so the next anchor cannot be taken from one
 * of them. Returns how many were dropped. Must be called with mux->lock held. */
static guint gva_streammux_drop_stale_head(GstGvaStreammux *mux, GvaStreammuxPadData *pdata) {
    if (!GST_CLOCK_TIME_IS_VALID(mux->last_pushed_batch_pts))
        return 0;

    guint dropped = 0;
    for (;;) {
        GvaStreammuxQueueItem *head = (GvaStreammuxQueueItem *)g_queue_peek_head(&pdata->buffer_queue);
        if (!head)
            break;
        GstClockTime pts = GST_BUFFER_PTS(head->buffer);
        if (!GST_CLOCK_TIME_IS_VALID(pts) || pts >= mux->last_pushed_batch_pts)
            break;

        GST_DEBUG_OBJECT(mux,
                         "Dropping stale buffer from sink_%u (pts=%" GST_TIME_FORMAT
                         " is behind the last batch at %" GST_TIME_FORMAT ")",
                         pdata->pad_index, GST_TIME_ARGS(pts), GST_TIME_ARGS(mux->last_pushed_batch_pts));
        queue_item_free((GvaStreammuxQueueItem *)g_queue_pop_head(&pdata->buffer_queue));
        dropped++;
    }

    if (dropped)
        g_cond_broadcast(&mux->cond); /* the queue has room again */
    return dropped;
}

/* A state change worth telling the application about. Collected under the
 * lock and posted after it is released, because posting reaches the bus and
 * from there whatever the application connected to it. */
typedef struct {
    guint pad_index;
    GvaStreammuxPadState state;
} GvaStreammuxPadTransition;

static const gchar *pad_state_name(GvaStreammuxPadState state) {
    switch (state) {
    case GVA_STREAMMUX_PAD_STALLED:
        return "stalled";
    case GVA_STREAMMUX_PAD_LATE:
        return "late";
    default:
        return "active";
    }
}

/* Fold what the batch just assembled says about each pad into its state.
 *
 * Liveness is judged on arrival, not on contribution. A source coming back
 * from a stall delivers a backlog too old to be batched, so a recovery
 * condition of "contributed to a batch" would never fire for it and it would
 * stay excluded for good -- while the stale-head discard quietly ate the
 * entire stream. Arrival says the source is alive; the discard is what gets it
 * current again.
 *
 * Must be called with mux->lock held. */
static void gva_streammux_update_pad_states(GstGvaStreammux *mux, GArray *transitions) {
    for (guint i = 0; i < mux->pad_data->len; i++) {
        GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
        if (!pdata)
            continue;

        GvaStreammuxPadState before = pdata->state;

        if (pdata->delivered) {
            pdata->idle_windows = 0;
            if (pdata->state == GVA_STREAMMUX_PAD_STALLED)
                pdata->state = GVA_STREAMMUX_PAD_ACTIVE;
        } else if (!pdata->eos) {
            pdata->idle_windows++;
            if (pdata->idle_windows >= GVA_STREAMMUX_STALL_WINDOWS)
                pdata->state = GVA_STREAMMUX_PAD_STALLED;
        }

        if (pdata->contributed) {
            pdata->late_rounds = 0;
            if (pdata->state == GVA_STREAMMUX_PAD_LATE)
                pdata->state = GVA_STREAMMUX_PAD_ACTIVE;
        } else if (pdata->stale_dropped) {
            pdata->late_rounds++;
            if (pdata->late_rounds >= GVA_STREAMMUX_LATE_ROUNDS && pdata->state != GVA_STREAMMUX_PAD_STALLED)
                pdata->state = GVA_STREAMMUX_PAD_LATE;
        }

        if (pdata->state != before) {
            GST_INFO_OBJECT(mux, "Pad sink_%u is now %s (was %s)", pdata->pad_index, pad_state_name(pdata->state),
                            pad_state_name(before));
            GvaStreammuxPadTransition transition = {pdata->pad_index, pdata->state};
            g_array_append_val(transitions, transition);
        }

        pdata->delivered = FALSE;
        pdata->contributed = FALSE;
        pdata->stale_dropped = FALSE;
    }
}

/* How many sink pads are currently delivering normally. Must be called with
 * mux->lock held. */
static guint count_active_pads(GstGvaStreammux *mux) {
    guint active = 0;
    for (guint i = 0; i < mux->pad_data->len; i++) {
        GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
        if (pdata && pdata->state == GVA_STREAMMUX_PAD_ACTIVE)
            active++;
    }
    return active;
}

/* Tell the application that a source changed state. An element message rather
 * than a signal: it reaches gst-launch and any application that watches the
 * bus, without adding API. Posted outside mux->lock. */
static void gva_streammux_post_stream_state(GstGvaStreammux *mux, const GvaStreammuxPadTransition *transition,
                                            guint active_pads, guint total_pads) {
    GstStructure *s = gst_structure_new("gva-streammux-stream-state", "pad-index", G_TYPE_UINT, transition->pad_index,
                                        "state", G_TYPE_STRING, pad_state_name(transition->state), "active-pads",
                                        G_TYPE_UINT, active_pads, "total-pads", G_TYPE_UINT, total_pads, NULL);
    gst_element_post_message(GST_ELEMENT(mux), gst_message_new_element(GST_OBJECT(mux), s));
}

/* Batch output data for pushing outside the lock */
typedef struct {
    GstBuffer *buf;
    guint pad_index;
    GstCaps *caps; /* own ref; only used in CONTAINER mode */
} BatchEntry;

/* Output task loop: runs on srcpad task thread */
static void gst_gva_streammux_output_loop(gpointer user_data) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(user_data);

    g_mutex_lock(&mux->lock);

    if (mux->flushing) {
        g_mutex_unlock(&mux->lock);
        return;
    }

    /* Gate: don't assemble or push anything until stream-start, caps and
     * segment have been pushed on the source pad. Gating on caps_negotiated
     * instead would let this task through as soon as the negotiating thread
     * sets that flag, which happens under the lock while the events can only
     * be pushed after it is released -- long enough to get a buffer out in
     * front of them. */
    if (!mux->events_pushed) {
        /* Degenerate case: every pad reached EOS before any caps were seen, so
         * caps can never be negotiated. End the stream instead of waiting
         * forever -- but end it properly. Pushing a bare EOS left downstream
         * with no stream-start and no segment at all, which is not a well
         * formed stream even when it carries no data. CONTAINER mode can also
         * state its caps here, since they do not depend on any sink pad. */
        if (mux->num_sink_pads > 0 && count_eos_pads(mux) >= mux->num_sink_pads) {
            gboolean need_stream_start = mux->send_stream_start;
            gboolean need_segment = !mux->segment_sent;
            GstSegment segment = mux->segment;
            GstCaps *caps = NULL;

            if (mux->output_mode == GVA_STREAMMUX_OUTPUT_CONTAINER)
                caps = gst_caps_from_string(STREAMMUX_BATCH_CAPS);
            mux->send_stream_start = FALSE;
            mux->segment_sent = TRUE;
            g_mutex_unlock(&mux->lock);

            GST_INFO_OBJECT(mux, "All pads reached EOS before any caps; ending the stream");
            if (need_stream_start) {
                gchar *stream_id = g_strdup_printf("gvastreammux/%08x%08x", g_random_int(), g_random_int());
                gst_pad_push_event(mux->srcpad, gst_event_new_stream_start(stream_id));
                g_free(stream_id);
            }
            if (caps) {
                gst_pad_push_event(mux->srcpad, gst_event_new_caps(caps));
                gst_caps_unref(caps);
            }
            if (need_segment)
                gst_pad_push_event(mux->srcpad, gst_event_new_segment(&segment));
            gst_pad_push_event(mux->srcpad, gst_event_new_eos());
            gst_pad_pause_task(mux->srcpad);
            return;
        }
        g_cond_wait(&mux->cond, &mux->lock);
        g_mutex_unlock(&mux->lock);
        return;
    }
    GvaStreammuxOutputMode output_mode = mux->output_mode;

    /* Phase 1: Determine batch anchor PTS (earliest PTS across all queues) */
    if (!GST_CLOCK_TIME_IS_VALID(mux->batch_anchor_pts)) {
        GstClockTime earliest = GST_CLOCK_TIME_NONE;
        gboolean any_buffer = FALSE;
        gboolean dropped_any = FALSE;

        for (guint i = 0; i < mux->pad_data->len; i++) {
            GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
            /* Skip a pad only once it is EOS *and* drained: buffers already
             * queued before EOS must still be batched, not discarded. */
            if (!pdata || (pdata->eos && g_queue_is_empty(&pdata->buffer_queue)))
                continue;

            /* Discard queue heads that the batches already pushed have left
             * behind. The chain function applies the same rule, but it applies
             * it when a buffer arrives: one queued while the last batch was
             * still older passes that check and then sits in the queue until
             * it is picked up here, by which time it is stale. Taking it as
             * the anchor would move batch time backwards -- and in CONTAINER
             * mode the anchor is the output buffer's PTS, so downstream would
             * see time going backwards.
             *
             * This doubles as the catch-up path for a source returning from a
             * stall with a backlog: its arrears are dropped until what it is
             * delivering is current again. */
            if (gva_streammux_drop_stale_head(mux, pdata) > 0) {
                pdata->stale_dropped = TRUE;
                dropped_any = TRUE;
            }

            GvaStreammuxQueueItem *head = (GvaStreammuxQueueItem *)g_queue_peek_head(&pdata->buffer_queue);
            if (head) {
                any_buffer = TRUE;
                GstClockTime pts = GST_BUFFER_PTS(head->buffer);
                if (GST_CLOCK_TIME_IS_VALID(pts) && (!GST_CLOCK_TIME_IS_VALID(earliest) || pts < earliest)) {
                    earliest = pts;
                }
            }
        }

        if (!any_buffer && dropped_any) {
            /* Everything queued was behind the last batch, which cannot be
             * right: that reference comes from the anchor, and the anchor is
             * the earliest queue head, so a single buffer carrying an absurd
             * timestamp -- arriving when it was briefly the only one queued --
             * becomes the reference and leaves every real buffer looking
             * stale. The element then discards its whole input for good, and
             * silently, because the batching loop is also where the stall
             * detection runs and it never completes a batch.
             *
             * Forget the reference and anchor again from whatever arrives
             * next. That costs the buffers discarded this round and bounds
             * the damage to it, however the bad reference came about. */
            GST_WARNING_OBJECT(mux,
                               "Every queued buffer was behind the last batch at %" GST_TIME_FORMAT
                               "; discarding that reference and re-anchoring",
                               GST_TIME_ARGS(mux->last_pushed_batch_pts));
            mux->last_pushed_batch_pts = GST_CLOCK_TIME_NONE;
        }

        if (!any_buffer) {
            /* All pads EOS and no remaining buffers -> send EOS downstream */
            if (mux->num_sink_pads > 0 && count_eos_pads(mux) >= mux->num_sink_pads) {
                g_mutex_unlock(&mux->lock);
                gst_pad_push_event(mux->srcpad, gst_event_new_eos());
                gst_pad_pause_task(mux->srcpad);
                return;
            }
            /* No data yet, wait indefinitely for signal */
            g_cond_wait(&mux->cond, &mux->lock);
            g_mutex_unlock(&mux->lock);
            return;
        }

        mux->batch_anchor_pts = earliest;
        mux->batch_start_real_time = g_get_monotonic_time();
    }

    /* Phase 2: Wait for other pads to contribute within timeout */
    gint64 deadline = mux->batch_start_real_time + (gint64)(mux->max_wait_time / 1000);

    guint contributing_count = 0;
    guint eligible_pads = 0;
    gboolean timed_out = FALSE;

    while (!mux->flushing) {
        contributing_count = 0;
        eligible_pads = 0;

        for (guint i = 0; i < mux->pad_data->len; i++) {
            GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
            /* A drained EOS pad is no longer eligible; an EOS pad that still has
             * queued buffers must be waited on like any live pad. */
            if (!pdata || (pdata->eos && g_queue_is_empty(&pdata->buffer_queue)))
                continue;
            /* A pad that is not delivering, or is delivering only arrears, is
             * still collected below whenever it has something that matches --
             * it just stops the batch waiting on it. */
            if (pdata->state != GVA_STREAMMUX_PAD_ACTIVE)
                continue;
            eligible_pads++;
            GvaStreammuxQueueItem *head = (GvaStreammuxQueueItem *)g_queue_peek_head(&pdata->buffer_queue);
            if (head) {
                GstClockTime pts = GST_BUFFER_PTS(head->buffer);
                if (!GST_CLOCK_TIME_IS_VALID(pts) || !GST_CLOCK_TIME_IS_VALID(mux->batch_anchor_pts) ||
                    pts_abs_diff(pts, mux->batch_anchor_pts) <= mux->pts_tolerance) {
                    contributing_count++;
                }
            }
        }

        if (contributing_count >= eligible_pads) {
            break;
        }

        gint64 now = g_get_monotonic_time();
        if (now >= deadline) {
            timed_out = TRUE;
            break;
        }

        g_cond_wait_until(&mux->cond, &mux->lock, deadline);
    }

    if (mux->flushing) {
        g_mutex_unlock(&mux->lock);
        return;
    }

    /* Phase 3: Collect matching buffers */
    GArray *batch = g_array_new(FALSE, FALSE, sizeof(BatchEntry));
    guint batch_size = 0;

    for (guint i = 0; i < mux->pad_data->len; i++) {
        GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
        if (!pdata || (pdata->eos && g_queue_is_empty(&pdata->buffer_queue)))
            continue;
        GvaStreammuxQueueItem *head = (GvaStreammuxQueueItem *)g_queue_peek_head(&pdata->buffer_queue);
        if (head) {
            GstClockTime pts = GST_BUFFER_PTS(head->buffer);
            if (!GST_CLOCK_TIME_IS_VALID(pts) || !GST_CLOCK_TIME_IS_VALID(mux->batch_anchor_pts) ||
                pts_abs_diff(pts, mux->batch_anchor_pts) <= mux->pts_tolerance) {
                head = (GvaStreammuxQueueItem *)g_queue_pop_head(&pdata->buffer_queue);
                /* Only CONTAINER mode carries per-stream caps in the meta; the
                 * entry takes over the item's buffer ref either way. */
                BatchEntry entry = {head->buffer, pdata->pad_index, NULL};
                if (output_mode == GVA_STREAMMUX_OUTPUT_CONTAINER && head->caps)
                    entry.caps = gst_caps_ref(head->caps);
                if (head->caps)
                    gst_caps_unref(head->caps);
                g_free(head);
                g_array_append_val(batch, entry);
                batch_size++;
                pdata->contributed = TRUE;
            }
        }
    }

    GstClockTime batch_pts = mux->batch_anchor_pts;

    GArray *transitions = g_array_new(FALSE, FALSE, sizeof(GvaStreammuxPadTransition));
    gva_streammux_update_pad_states(mux, transitions);
    guint active_pads = count_active_pads(mux);
    guint total_pads = mux->num_sink_pads;

    /* One line per batch, with everything needed to explain why it looks the
     * way it does: the anchor the batch was built around, how many of the
     * eligible pads actually made it in, and how many sink pads exist at all.
     * A partial batch is the interesting case -- it means a pad was eligible
     * but had nothing within pts-tolerance of the anchor before the wait ran
     * out -- so say so explicitly rather than leaving it to be inferred from
     * two numbers differing. */
    GST_DEBUG_OBJECT(mux, "Batch anchor=%" GST_TIME_FORMAT " size=%u/%u eligible, %u pad(s) total%s%s",
                     GST_TIME_ARGS(batch_pts), batch_size, eligible_pads, mux->num_sink_pads,
                     batch_size < eligible_pads ? " [PARTIAL]" : "", timed_out ? " [TIMEOUT]" : "");

    /* The anchor is the earliest queue head each round, so a source that goes
     * quiet and then delivers a burst can pull it back behind the batch just
     * pushed: while that source had nothing queued the anchor followed the
     * others, and its older buffers become the anchor once they arrive. In
     * CONTAINER mode the anchor is the output buffer's PTS, so this reaches
     * downstream as time going backwards.
     *
     * The chain function drops a buffer more than pts-tolerance behind the
     * last batch, but it checks that on arrival, not when the buffer is
     * finally batched -- a buffer queued while the last batch was still older
     * passes the check and can regress the anchor by more than the tolerance.
     * Measured at 33 ms with a 20 ms tolerance and a source stalling for
     * 60 ms every tenth buffer. */
    if (GST_CLOCK_TIME_IS_VALID(batch_pts) && GST_CLOCK_TIME_IS_VALID(mux->last_pushed_batch_pts) &&
        batch_pts < mux->last_pushed_batch_pts) {
        GstClockTime regression = mux->last_pushed_batch_pts - batch_pts;
        if (!mux->anchor_regression_warned) {
            mux->anchor_regression_warned = TRUE;
            GST_WARNING_OBJECT(
                mux,
                "Batch anchor went backwards by %" GST_TIME_FORMAT " (%" GST_TIME_FORMAT " after %" GST_TIME_FORMAT
                "): a source is delivering in bursts. Further occurrences are logged at debug level.",
                GST_TIME_ARGS(regression), GST_TIME_ARGS(batch_pts), GST_TIME_ARGS(mux->last_pushed_batch_pts));
        } else {
            GST_DEBUG_OBJECT(mux, "Batch anchor went backwards by %" GST_TIME_FORMAT, GST_TIME_ARGS(regression));
        }
    }

    mux->last_pushed_batch_pts = mux->batch_anchor_pts;
    mux->batch_anchor_pts = GST_CLOCK_TIME_NONE;

    /* Wake up chain functions blocked on full queues */
    g_cond_broadcast(&mux->cond);

    g_mutex_unlock(&mux->lock);

    for (guint i = 0; i < transitions->len; i++)
        gva_streammux_post_stream_state(mux, &g_array_index(transitions, GvaStreammuxPadTransition, i), active_pads,
                                        total_pads);
    g_array_free(transitions, TRUE);

    /* Phase 4: Push buffers downstream (outside lock) */
    if (batch_size == 0) {
        g_array_free(batch, TRUE);
        return;
    }

    gst_gva_streammux_apply_fps_throttle(mux);

    GstFlowReturn ret = GST_FLOW_OK;

    if (output_mode == GVA_STREAMMUX_OUTPUT_CONTAINER) {
        /* CONTAINER mode: pack the whole batch into a single empty buffer whose
         * GstAnalyticsBatchMeta carries one stream per source. Each stream holds
         * the source buffer (objects[0]) and its caps (a CAPS sticky event), so
         * downstream can reconstruct heterogeneous streams (e.g. video + lidar).
         * Ownership of every source buffer and caps ref transfers into the meta;
         * gst_analytics_batch_meta_free unrefs them. */
        GstBuffer *container = gst_buffer_new();
        GST_BUFFER_PTS(container) = batch_pts;

        GstAnalyticsBatchMeta *meta = gst_buffer_add_analytics_batch_meta(container);
        if (!meta) {
            GST_ERROR_OBJECT(mux, "Failed to add GstAnalyticsBatchMeta to container buffer");
            gst_buffer_unref(container);
            for (guint i = 0; i < batch->len; i++) {
                BatchEntry *e = &g_array_index(batch, BatchEntry, i);
                if (e->buf)
                    gst_buffer_unref(e->buf);
                if (e->caps)
                    gst_caps_unref(e->caps);
            }
            g_array_free(batch, TRUE);
            gst_pad_pause_task(mux->srcpad);
            return;
        }

        meta->streams = g_new0(GstAnalyticsBatchStream, batch->len);
        meta->n_streams = batch->len;
        for (guint i = 0; i < batch->len; i++) {
            BatchEntry *entry = &g_array_index(batch, BatchEntry, i);
            GstAnalyticsBatchStream *stream = &meta->streams[i];
            stream->index = entry->pad_index;

            stream->objects = g_new0(GstMiniObject *, 1);
            stream->objects[0] = GST_MINI_OBJECT_CAST(entry->buf); /* transfer ownership */
            stream->n_objects = 1;
            entry->buf = NULL;

            if (entry->caps) {
                stream->sticky_events = g_new0(GstEvent *, 1);
                stream->sticky_events[0] = gst_event_new_caps(entry->caps); /* takes its own ref */
                stream->n_sticky_events = 1;
                gst_caps_unref(entry->caps);
                entry->caps = NULL;
            }
        }

        GST_LOG_OBJECT(mux, "Push container buffer (pts=%" GST_TIME_FORMAT "), n_streams=%u", GST_TIME_ARGS(batch_pts),
                       batch_size);

        ret = gst_pad_push(mux->srcpad, container);
        if (ret != GST_FLOW_OK)
            GST_WARNING_OBJECT(mux, "Container push failed: %s", gst_flow_get_name(ret));

        g_array_free(batch, TRUE);
    } else {
        /* PASSTHROUGH mode (all-video): legacy behavior. Push each source buffer
         * as-is, tagging it with a single-stream GstAnalyticsBatchMeta so a
         * downstream gvastreamdemux can route by source index. */
        guint push_idx = 0;
        for (; push_idx < batch->len; push_idx++) {
            BatchEntry *entry = &g_array_index(batch, BatchEntry, push_idx);
            GstBuffer *buf = gst_buffer_make_writable(entry->buf);
            entry->buf = NULL;

            GstAnalyticsBatchMeta *meta = gst_buffer_add_analytics_batch_meta(buf);
            if (meta) {
                /* One stream: this buffer carries one source. n_streams used to
                 * be set to the batch size, with only streams[0] filled in, so
                 * every consumer that walks streams[0..n_streams) -- which is
                 * all of them -- saw batch_size-1 zeroed entries per buffer.
                 * How many sources were in the batch is a different fact and
                 * does not belong in this field. */
                meta->streams = g_new0(GstAnalyticsBatchStream, 1);
                meta->streams[0].index = entry->pad_index;
                meta->n_streams = 1;
            }

            GST_LOG_OBJECT(mux, "Push buffer from source %u (pts=%" GST_TIME_FORMAT "), batch_size=%u",
                           entry->pad_index, GST_TIME_ARGS(GST_BUFFER_PTS(buf)), batch_size);

            ret = gst_pad_push(mux->srcpad, buf);
            if (ret != GST_FLOW_OK) {
                GST_WARNING_OBJECT(mux, "Push failed for source %u: %s", entry->pad_index, gst_flow_get_name(ret));
                break;
            }
        }

        /* Unref buffers that were not pushed due to error */
        for (guint i = push_idx + 1; i < batch->len; i++) {
            BatchEntry *entry = &g_array_index(batch, BatchEntry, i);
            if (entry->buf) {
                gst_buffer_unref(entry->buf);
                entry->buf = NULL;
            }
        }

        g_array_free(batch, TRUE);
    }

    if (ret == GST_FLOW_OK) {
        gst_gva_streammux_update_output_time(mux);
        return;
    }

    /* Pausing the task is the end of the line: nothing downstream will ask
     * again, and upstream only ever sees GST_FLOW_OK from our chain function
     * because buffers are handed over through the queues. So whatever stopped
     * the push has to be turned into something visible here, or the pipeline
     * just stops with an empty bus and no EOS.
     *
     * GST_FLOW_FLUSHING is the exception -- it means a flush or a state change
     * is already tearing the stream down, and that path posts its own
     * messages. This mirrors what gstbasesrc does with its loop function. */
    GST_INFO_OBJECT(mux, "Pausing output task due to flow return: %s", gst_flow_get_name(ret));

    if (ret == GST_FLOW_EOS) {
        gst_pad_push_event(mux->srcpad, gst_event_new_eos());
    } else if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_EOS) {
        GST_ELEMENT_FLOW_ERROR(mux, ret);
        gst_pad_push_event(mux->srcpad, gst_event_new_eos());
    }

    gst_pad_pause_task(mux->srcpad);
}
