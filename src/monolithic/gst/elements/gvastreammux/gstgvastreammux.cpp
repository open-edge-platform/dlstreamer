/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvastreammux.h"
#include <gst/analytics/gstanalyticsbatchmeta.h>

#include <cstdio>
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
};

#define DEFAULT_MAX_FPS 0.0
#define DEFAULT_PTS_TOLERANCE (20 * GST_MSECOND)
#define DEFAULT_MAX_WAIT_TIME (40 * GST_MSECOND)
#define DEFAULT_MAX_QUEUE_SIZE 2
#define DEFAULT_SYNC_MODE GVA_STREAMMUX_SYNC_MODE_NONE

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

/* Pad templates */
#define STREAMMUX_VIDEO_CAPS                                                                                           \
    GST_VIDEO_CAPS_MAKE("{ BGRx, BGRA, BGR, NV12, I420, RGB, RGBA, RGBx }")                                            \
    "; " GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:VAMemory", "{ NV12 }") "; " GST_VIDEO_CAPS_MAKE_WITH_FEATURES(      \
        "memory:DMABuf", "{ DMA_DRM }") "; "

static GstStaticPadTemplate gva_streammux_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST, GST_STATIC_CAPS(STREAMMUX_VIDEO_CAPS));

static GstStaticPadTemplate gva_streammux_src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(STREAMMUX_VIDEO_CAPS));

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
static gboolean gst_gva_streammux_src_query(GstPad *pad, GstObject *parent, GstQuery *query);
static gboolean gst_gva_streammux_src_event(GstPad *pad, GstObject *parent, GstEvent *event);
static void gst_gva_streammux_output_loop(gpointer user_data);

G_DEFINE_TYPE(GstGvaStreammux, gst_gva_streammux, GST_TYPE_ELEMENT);

static inline GstClockTime pts_abs_diff(GstClockTime a, GstClockTime b) {
    return (a > b) ? (a - b) : (b - a);
}

static GvaStreammuxPadData *get_pad_data(GstGvaStreammux *mux, GstPad *pad) {
    return (GvaStreammuxPadData *)g_object_get_data(G_OBJECT(pad), "mux-pad-data");
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

    gst_element_class_set_static_metadata(element_class, "GVA Stream Muxer", "Video/Muxer",
                                          "Muxes multiple video streams with PTS-based synchronization",
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
}

static void gst_gva_streammux_init(GstGvaStreammux *mux) {
    mux->max_fps = DEFAULT_MAX_FPS;
    mux->pts_tolerance = DEFAULT_PTS_TOLERANCE;
    mux->max_wait_time = DEFAULT_MAX_WAIT_TIME;
    mux->max_queue_size = DEFAULT_MAX_QUEUE_SIZE;
    mux->sync_mode = DEFAULT_SYNC_MODE;

    mux->num_sink_pads = 0;
    mux->started = FALSE;
    mux->send_stream_start = TRUE;
    mux->flushing = FALSE;
    mux->flushing_pads_count = 0;
    mux->sinkpads = NULL;
    mux->current_caps = NULL;
    mux->segment_sent = FALSE;
    mux->last_output_time = GST_CLOCK_TIME_NONE;
    mux->max_fps_duration = GST_CLOCK_TIME_NONE;
    mux->batch_anchor_pts = GST_CLOCK_TIME_NONE;
    mux->batch_start_real_time = 0;
    mux->last_pushed_batch_pts = GST_CLOCK_TIME_NONE;
    mux->eos_pad_count = 0;

    g_mutex_init(&mux->lock);
    g_cond_init(&mux->cond);

    gst_segment_init(&mux->segment, GST_FORMAT_TIME);

    mux->pad_data = g_ptr_array_new();

    /* Create source pad */
    mux->srcpad = gst_pad_new_from_static_template(&gva_streammux_src_template, "src");
    gst_pad_set_query_function(mux->srcpad, GST_DEBUG_FUNCPTR(gst_gva_streammux_src_query));
    gst_pad_set_event_function(mux->srcpad, GST_DEBUG_FUNCPTR(gst_gva_streammux_src_event));
    gst_pad_use_fixed_caps(mux->srcpad);
    gst_element_add_pad(GST_ELEMENT(mux), mux->srcpad);
}

static void gst_gva_streammux_flush_pad_queues(GstGvaStreammux *mux) {
    for (guint i = 0; i < mux->pad_data->len; i++) {
        GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
        if (!pdata)
            continue;
        while (!g_queue_is_empty(&pdata->buffer_queue))
            gst_buffer_unref((GstBuffer *)g_queue_pop_head(&pdata->buffer_queue));
    }
}

static void gst_gva_streammux_finalize(GObject *object) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(object);

    gst_gva_streammux_flush_pad_queues(mux);

    for (guint i = 0; i < mux->pad_data->len; i++) {
        GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
        if (pdata) {
            g_queue_clear(&pdata->buffer_queue);
            g_free(pdata);
        }
    }
    g_ptr_array_free(mux->pad_data, TRUE);

    g_mutex_clear(&mux->lock);
    g_cond_clear(&mux->cond);

    if (mux->current_caps) {
        gst_caps_unref(mux->current_caps);
        mux->current_caps = NULL;
    }

    G_OBJECT_CLASS(gst_gva_streammux_parent_class)->finalize(object);
}

/* Property set/get */
static void gst_gva_streammux_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(object);

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
        mux->max_wait_time = g_value_get_uint64(value);
        break;
    case PROP_MAX_QUEUE_SIZE:
        mux->max_queue_size = g_value_get_uint(value);
        break;
    case PROP_SYNC_MODE:
        mux->sync_mode = (GvaStreammuxSyncMode)g_value_get_enum(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gva_streammux_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(object);

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
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* Normalize buffer PTS based on sync-mode. Caller must ensure buf is writable. */
static void normalize_buffer_pts(GstGvaStreammux *mux, GvaStreammuxPadData *pdata, GstBuffer *buf) {
    GstClockTime raw_pts = GST_BUFFER_PTS(buf);

    switch (mux->sync_mode) {
    case GVA_STREAMMUX_SYNC_MODE_FIRST_PTS:
        if (!GST_CLOCK_TIME_IS_VALID(raw_pts))
            return;
        if (!pdata->first_pts_set) {
            pdata->first_pts = raw_pts;
            pdata->first_pts_set = TRUE;
            GST_INFO_OBJECT(mux, "Pad sink_%u first_pts captured = %" GST_TIME_FORMAT, pdata->pad_index,
                            GST_TIME_ARGS(raw_pts));
        }
        if (raw_pts >= pdata->first_pts)
            GST_BUFFER_PTS(buf) = raw_pts - pdata->first_pts;
        break;

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

    g_mutex_lock(&mux->lock);

    if (req_name && sscanf(req_name, "sink_%u", &pad_index) == 1) {
        name = g_strdup(req_name);
    } else {
        pad_index = mux->num_sink_pads;
        name = g_strdup_printf("sink_%u", pad_index);
    }

    if (pad_index >= GST_GVA_STREAMMUX_MAX_PAD_INDEX) {
        GST_ERROR_OBJECT(mux, "Pad index %u exceeds maximum (%u). Use sink_0 to sink_%u.", pad_index,
                         GST_GVA_STREAMMUX_MAX_PAD_INDEX, GST_GVA_STREAMMUX_MAX_PAD_INDEX - 1);
        g_free(name);
        g_mutex_unlock(&mux->lock);
        return NULL;
    }

    sinkpad = gst_pad_new_from_static_template(&gva_streammux_sink_template, name);
    gst_pad_set_chain_function(sinkpad, GST_DEBUG_FUNCPTR(gst_gva_streammux_chain));
    gst_pad_set_event_function(sinkpad, GST_DEBUG_FUNCPTR(gst_gva_streammux_sink_event));

    /* Create per-pad data */
    GvaStreammuxPadData *pdata = g_new0(GvaStreammuxPadData, 1);
    pdata->pad = sinkpad;
    pdata->pad_index = pad_index;
    g_queue_init(&pdata->buffer_queue);
    pdata->eos = FALSE;
    pdata->flushing = FALSE;
    pdata->first_pts_set = FALSE;
    pdata->first_pts = GST_CLOCK_TIME_NONE;
    pdata->segment_start = GST_CLOCK_TIME_NONE;

    g_object_set_data(G_OBJECT(sinkpad), "mux-pad-data", pdata);

    /* Grow the array if needed */
    while (mux->pad_data->len <= pad_index)
        g_ptr_array_add(mux->pad_data, NULL);
    g_ptr_array_index(mux->pad_data, pad_index) = pdata;

    gst_pad_use_fixed_caps(sinkpad);
    GST_PAD_SET_PROXY_ALLOCATION(sinkpad);
    gst_element_add_pad(element, sinkpad);

    mux->sinkpads = g_list_append(mux->sinkpads, sinkpad);
    mux->num_sink_pads++;

    GST_INFO_OBJECT(mux, "Created sink pad %s (index=%u), total pads=%u", name, pad_index, mux->num_sink_pads);

    g_free(name);
    g_mutex_unlock(&mux->lock);

    return sinkpad;
}

static void gst_gva_streammux_release_pad(GstElement *element, GstPad *pad) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(element);

    g_mutex_lock(&mux->lock);

    GvaStreammuxPadData *pdata = get_pad_data(mux, pad);
    if (pdata) {
        if (pdata->flushing && mux->flushing_pads_count > 0)
            mux->flushing_pads_count--;
        while (!g_queue_is_empty(&pdata->buffer_queue))
            gst_buffer_unref((GstBuffer *)g_queue_pop_head(&pdata->buffer_queue));
        if (pdata->pad_index < mux->pad_data->len)
            g_ptr_array_index(mux->pad_data, pdata->pad_index) = NULL;
        g_queue_clear(&pdata->buffer_queue);
        g_free(pdata);
    }

    mux->sinkpads = g_list_remove(mux->sinkpads, pad);
    mux->num_sink_pads--;

    gst_element_remove_pad(element, pad);

    GST_INFO_OBJECT(mux, "Released pad, remaining pads=%u", mux->num_sink_pads);

    g_mutex_unlock(&mux->lock);
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
        mux->started = FALSE;
        mux->send_stream_start = TRUE;
        mux->segment_sent = FALSE;
        mux->flushing = FALSE;
        mux->last_output_time = GST_CLOCK_TIME_NONE;
        mux->batch_anchor_pts = GST_CLOCK_TIME_NONE;
        mux->last_pushed_batch_pts = GST_CLOCK_TIME_NONE;
        mux->eos_pad_count = 0;
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
    default:
        break;
    }

    ret = GST_ELEMENT_CLASS(gst_gva_streammux_parent_class)->change_state(element, transition);

    switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        g_mutex_lock(&mux->lock);
        mux->flushing = TRUE;
        g_cond_broadcast(&mux->cond);
        g_mutex_unlock(&mux->lock);
        gst_pad_stop_task(mux->srcpad);
        g_mutex_lock(&mux->lock);
        gst_gva_streammux_flush_pad_queues(mux);
        mux->started = FALSE;
        mux->flushing_pads_count = 0;
        for (guint i = 0; i < mux->pad_data->len; i++) {
            GvaStreammuxPadData *pd = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
            if (pd) {
                pd->flushing = FALSE;
                pd->first_pts_set = FALSE;
                pd->first_pts = GST_CLOCK_TIME_NONE;
                pd->segment_start = GST_CLOCK_TIME_NONE;
            }
        }
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

/* Sink event handler */
static gboolean gst_gva_streammux_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(parent);
    gboolean ret = TRUE;

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
        GstCaps *caps = NULL;
        gst_event_parse_caps(event, &caps);
        GvaStreammuxPadData *pdata = get_pad_data(mux, pad);
        guint pad_index = pdata ? pdata->pad_index : 0;
        GST_INFO_OBJECT(mux, "Received caps on pad sink_%u: %" GST_PTR_FORMAT, pad_index, caps);

        gboolean need_stream_start = FALSE;
        gboolean need_segment = FALSE;
        GstCaps *caps_to_push = NULL;

        g_mutex_lock(&mux->lock);
        if (!mux->current_caps) {
            mux->current_caps = gst_caps_copy(caps);
            caps_to_push = gst_caps_ref(mux->current_caps);
            need_stream_start = mux->send_stream_start;
            mux->send_stream_start = FALSE;
            need_segment = !mux->segment_sent;
            mux->segment_sent = TRUE;
            if (need_segment)
                gst_segment_init(&mux->segment, GST_FORMAT_TIME);
        }
        g_mutex_unlock(&mux->lock);

        if (caps_to_push) {
            if (need_stream_start) {
                gchar *stream_id = g_strdup_printf("gvastreammux/%08x%08x", g_random_int(), g_random_int());
                gst_pad_push_event(mux->srcpad, gst_event_new_stream_start(stream_id));
                g_free(stream_id);
                GST_INFO_OBJECT(mux, "Sent stream-start event");
            }
            gst_pad_push_event(mux->srcpad, gst_event_new_caps(caps_to_push));
            gst_caps_unref(caps_to_push);
            GST_INFO_OBJECT(mux, "Set output caps: %" GST_PTR_FORMAT, mux->current_caps);
            if (need_segment) {
                gst_pad_push_event(mux->srcpad, gst_event_new_segment(&mux->segment));
                GST_INFO_OBJECT(mux, "Sent segment event");
            }
        }

        gst_event_unref(event);
        ret = TRUE;
        break;
    }
    case GST_EVENT_SEGMENT: {
        GstSegment seg;
        gst_event_copy_segment(event, &seg);
        g_mutex_lock(&mux->lock);
        GvaStreammuxPadData *pdata = get_pad_data(mux, pad);
        if (pdata) {
            pdata->segment_start = seg.start;
            GST_DEBUG_OBJECT(mux, "Pad sink_%u segment.start=%" GST_TIME_FORMAT, pdata->pad_index,
                             GST_TIME_ARGS(seg.start));
        }
        g_mutex_unlock(&mux->lock);
        gst_event_unref(event);
        ret = TRUE;
        break;
    }
    case GST_EVENT_EOS: {
        g_mutex_lock(&mux->lock);
        GvaStreammuxPadData *pdata = get_pad_data(mux, pad);
        if (pdata && !pdata->eos) {
            pdata->eos = TRUE;
            mux->eos_pad_count++;
            GST_INFO_OBJECT(mux, "EOS on pad sink_%u, eos_count=%u/%u", pdata->pad_index, mux->eos_pad_count,
                            mux->num_sink_pads);
        }
        g_cond_signal(&mux->cond);
        g_mutex_unlock(&mux->lock);
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
            gst_pad_pause_task(mux->srcpad);
            ret = gst_pad_push_event(mux->srcpad, event);
        } else {
            gst_event_unref(event);
            ret = TRUE;
        }
        break;
    }
    case GST_EVENT_FLUSH_STOP: {
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
                    pd->first_pts_set = FALSE;
                    pd->first_pts = GST_CLOCK_TIME_NONE;
                }
            }
            mux->eos_pad_count = 0;
            mux->batch_anchor_pts = GST_CLOCK_TIME_NONE;
            mux->last_pushed_batch_pts = GST_CLOCK_TIME_NONE;
        }
        g_mutex_unlock(&mux->lock);

        if (last_flush) {
            ret = gst_pad_push_event(mux->srcpad, event);
            gst_pad_start_task(mux->srcpad, gst_gva_streammux_output_loop, mux, NULL);
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
    GvaStreammuxPadData *pdata = get_pad_data(mux, pad);

    if (!pdata) {
        GST_ERROR_OBJECT(mux, "No pad data for pad %s", GST_PAD_NAME(pad));
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    if (mux->sync_mode != GVA_STREAMMUX_SYNC_MODE_NONE) {
        buf = gst_buffer_make_writable(buf);
        normalize_buffer_pts(mux, pdata, buf);
    }

    g_mutex_lock(&mux->lock);

    if (mux->flushing) {
        g_mutex_unlock(&mux->lock);
        gst_buffer_unref(buf);
        return GST_FLOW_FLUSHING;
    }

    /* Back-pressure: block if queue is full */
    while (g_queue_get_length(&pdata->buffer_queue) >= mux->max_queue_size && !mux->flushing) {
        g_cond_wait(&mux->cond, &mux->lock);
    }

    if (mux->flushing) {
        g_mutex_unlock(&mux->lock);
        gst_buffer_unref(buf);
        return GST_FLOW_FLUSHING;
    }

    GstClockTime pts = GST_BUFFER_PTS(buf);

    /* Late frame detection */
    if (GST_CLOCK_TIME_IS_VALID(pts) && GST_CLOCK_TIME_IS_VALID(mux->last_pushed_batch_pts)) {
        if (pts + mux->pts_tolerance < mux->last_pushed_batch_pts) {
            GST_WARNING_OBJECT(mux,
                               "Late frame from pad sink_%u (pts=%" GST_TIME_FORMAT
                               " < last_batch_pts=%" GST_TIME_FORMAT "), dropping",
                               pdata->pad_index, GST_TIME_ARGS(pts), GST_TIME_ARGS(mux->last_pushed_batch_pts));
            g_mutex_unlock(&mux->lock);
            gst_buffer_unref(buf);
            return GST_FLOW_OK;
        }
    }

    g_queue_push_tail(&pdata->buffer_queue, buf);
    g_cond_signal(&mux->cond);

    g_mutex_unlock(&mux->lock);
    return GST_FLOW_OK;
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

        min_latency += mux->max_wait_time;
        if (GST_CLOCK_TIME_IS_VALID(max_latency))
            max_latency += mux->max_wait_time;

        gst_query_set_latency(query, live, min_latency, max_latency);
        return TRUE;
    }
    case GST_QUERY_CAPS: {
        GstCaps *filter;
        gst_query_parse_caps(query, &filter);
        GstCaps *caps = gst_pad_get_pad_template_caps(pad);
        if (mux->current_caps) {
            GstCaps *result = gst_caps_intersect(caps, mux->current_caps);
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

/* Apply max-fps throttling */
static void gst_gva_streammux_apply_fps_throttle(GstGvaStreammux *mux) {
    if (!GST_CLOCK_TIME_IS_VALID(mux->max_fps_duration))
        return;

    GstClock *clock = gst_element_get_clock(GST_ELEMENT(mux));
    if (!clock)
        return;

    GstClockTime now = gst_clock_get_time(clock);
    gst_object_unref(clock);

    if (GST_CLOCK_TIME_IS_VALID(mux->last_output_time)) {
        GstClockTime elapsed = now - mux->last_output_time;
        if (elapsed < mux->max_fps_duration) {
            GstClockTime wait = mux->max_fps_duration - elapsed;
            GST_LOG_OBJECT(mux, "FPS throttle: waiting %" GST_TIME_FORMAT, GST_TIME_ARGS(wait));
            g_usleep(GST_TIME_AS_USECONDS(wait));
        }
    }
}

static void gst_gva_streammux_update_output_time(GstGvaStreammux *mux) {
    GstClock *clock = gst_element_get_clock(GST_ELEMENT(mux));
    if (clock) {
        mux->last_output_time = gst_clock_get_time(clock);
        gst_object_unref(clock);
    }
}

/* Batch output data for pushing outside the lock */
typedef struct {
    GstBuffer *buf;
    guint pad_index;
} BatchEntry;

/* Output task loop: runs on srcpad task thread */
static void gst_gva_streammux_output_loop(gpointer user_data) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(user_data);

    g_mutex_lock(&mux->lock);

    if (mux->flushing) {
        g_mutex_unlock(&mux->lock);
        return;
    }

    /* Phase 1: Determine batch anchor PTS (earliest PTS across all queues) */
    if (!GST_CLOCK_TIME_IS_VALID(mux->batch_anchor_pts)) {
        GstClockTime earliest = GST_CLOCK_TIME_NONE;
        gboolean any_buffer = FALSE;

        for (guint i = 0; i < mux->pad_data->len; i++) {
            GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
            if (!pdata || pdata->eos)
                continue;
            GstBuffer *head = (GstBuffer *)g_queue_peek_head(&pdata->buffer_queue);
            if (head) {
                any_buffer = TRUE;
                GstClockTime pts = GST_BUFFER_PTS(head);
                if (GST_CLOCK_TIME_IS_VALID(pts) && (!GST_CLOCK_TIME_IS_VALID(earliest) || pts < earliest)) {
                    earliest = pts;
                }
            }
        }

        if (!any_buffer) {
            /* All pads EOS and no remaining buffers -> send EOS downstream */
            if (mux->eos_pad_count >= mux->num_sink_pads && mux->num_sink_pads > 0) {
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

    while (!mux->flushing) {
        guint contributing_count = 0;
        guint eligible_pads = 0;

        for (guint i = 0; i < mux->pad_data->len; i++) {
            GvaStreammuxPadData *pdata = (GvaStreammuxPadData *)g_ptr_array_index(mux->pad_data, i);
            if (!pdata || pdata->eos)
                continue;
            eligible_pads++;
            GstBuffer *head = (GstBuffer *)g_queue_peek_head(&pdata->buffer_queue);
            if (head) {
                GstClockTime pts = GST_BUFFER_PTS(head);
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
            GST_LOG_OBJECT(mux, "Batch timeout: got %u/%u pads", contributing_count, eligible_pads);
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
        if (!pdata || pdata->eos)
            continue;
        GstBuffer *head = (GstBuffer *)g_queue_peek_head(&pdata->buffer_queue);
        if (head) {
            GstClockTime pts = GST_BUFFER_PTS(head);
            if (!GST_CLOCK_TIME_IS_VALID(pts) || !GST_CLOCK_TIME_IS_VALID(mux->batch_anchor_pts) ||
                pts_abs_diff(pts, mux->batch_anchor_pts) <= mux->pts_tolerance) {
                head = (GstBuffer *)g_queue_pop_head(&pdata->buffer_queue);
                BatchEntry entry = {head, pdata->pad_index};
                g_array_append_val(batch, entry);
                batch_size++;
            }
        }
    }

    mux->last_pushed_batch_pts = mux->batch_anchor_pts;
    mux->batch_anchor_pts = GST_CLOCK_TIME_NONE;

    /* Wake up chain functions blocked on full queues */
    g_cond_broadcast(&mux->cond);

    g_mutex_unlock(&mux->lock);

    /* Phase 4: Push buffers downstream (outside lock) */
    if (batch_size == 0) {
        g_array_free(batch, TRUE);
        return;
    }

    gst_gva_streammux_apply_fps_throttle(mux);

    GstFlowReturn ret = GST_FLOW_OK;
    guint push_idx = 0;
    for (; push_idx < batch->len; push_idx++) {
        BatchEntry *entry = &g_array_index(batch, BatchEntry, push_idx);
        GstBuffer *buf = gst_buffer_make_writable(entry->buf);
        entry->buf = NULL;

        GstAnalyticsBatchMeta *meta = gst_buffer_add_analytics_batch_meta(buf);
        if (meta) {
            meta->streams = g_new0(GstAnalyticsBatchStream, batch_size);
            meta->streams[0].index = entry->pad_index;
            meta->n_streams = batch_size;
        }

        GST_LOG_OBJECT(mux, "Push buffer from source %u (pts=%" GST_TIME_FORMAT "), batch_size=%u", entry->pad_index,
                       GST_TIME_ARGS(GST_BUFFER_PTS(buf)), batch_size);

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

    if (ret == GST_FLOW_OK)
        gst_gva_streammux_update_output_time(mux);

    if (ret != GST_FLOW_OK) {
        GST_INFO_OBJECT(mux, "Pausing output task due to flow return: %s", gst_flow_get_name(ret));
        gst_pad_pause_task(mux->srcpad);
    }
}
