/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvastreammux.h"
#include "dlstreamer/gst/metadata/gstgvastreammuxmeta.h"

#include <cstdio>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC(gst_gva_streammux_debug);
#define GST_CAT_DEFAULT gst_gva_streammux_debug

/* Properties */
enum {
    PROP_0,
    PROP_MAX_SAME_SOURCE_FRAMES,
    PROP_MAX_FPS,
    PROP_MIN_FPS,
    PROP_SYNC_INPUTS,
    PROP_MAX_LATENCY,
    PROP_FRAME_DURATION,
};

#define DEFAULT_MAX_SAME_SOURCE_FRAMES 1
#define DEFAULT_MAX_FPS 0.0
#define DEFAULT_MIN_FPS 0.0
#define DEFAULT_SYNC_INPUTS FALSE
#define DEFAULT_MAX_LATENCY 0
#define DEFAULT_FRAME_DURATION GST_CLOCK_TIME_NONE

/* Pad templates */
#define STREAMMUX_VIDEO_CAPS                                                                                           \
    GST_VIDEO_CAPS_MAKE("{ BGRx, BGRA, BGR, NV12, I420, RGB, RGBA, RGBx }") "; "                                      \
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:VAMemory", "{ NV12 }") "; "                                          \
            GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:DMABuf", "{ DMA_DRM }") "; "

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
static GstFlowReturn gst_gva_streammux_collected(GstCollectPads *pads, gpointer user_data);
static gboolean gst_gva_streammux_sink_event(GstCollectPads *pads, GstCollectData *data, GstEvent *event,
                                              gpointer user_data);
static gboolean gst_gva_streammux_src_query(GstPad *pad, GstObject *parent, GstQuery *query);
static gboolean gst_gva_streammux_src_event(GstPad *pad, GstObject *parent, GstEvent *event);

G_DEFINE_TYPE(GstGvaStreammux, gst_gva_streammux, GST_TYPE_ELEMENT);

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
                                          "Muxes multiple video streams into a single pipeline with source metadata",
                                          "Intel Corporation");

    /* Properties */
    g_object_class_install_property(
        gobject_class, PROP_MAX_SAME_SOURCE_FRAMES,
        g_param_spec_uint("max-same-source-frames", "Max Same Source Frames",
                          "Maximum number of frames from a single source per batch cycle", 1, G_MAXUINT,
                          DEFAULT_MAX_SAME_SOURCE_FRAMES, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_MAX_FPS,
        g_param_spec_double("max-fps", "Max FPS", "Maximum output frame rate (0 = unlimited)", 0.0, G_MAXDOUBLE,
                            DEFAULT_MAX_FPS, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_MIN_FPS,
        g_param_spec_double("min-fps", "Min FPS", "Minimum output frame rate (0 = no minimum)", 0.0, G_MAXDOUBLE,
                            DEFAULT_MIN_FPS, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_SYNC_INPUTS,
        g_param_spec_boolean("sync-inputs", "Sync Inputs", "Synchronize input streams by buffer timestamps",
                             DEFAULT_SYNC_INPUTS, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_MAX_LATENCY,
        g_param_spec_uint64("max-latency", "Max Latency", "Maximum additional latency in nanoseconds", 0, G_MAXUINT64,
                            DEFAULT_MAX_LATENCY, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
        gobject_class, PROP_FRAME_DURATION,
        g_param_spec_uint64("frame-duration", "Frame Duration",
                            "Expected frame duration in nanoseconds (0 or NONE = auto from caps)", 0, G_MAXUINT64,
                            DEFAULT_FRAME_DURATION, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_gva_streammux_init(GstGvaStreammux *mux) {
    mux->max_same_source_frames = DEFAULT_MAX_SAME_SOURCE_FRAMES;
    mux->max_fps = DEFAULT_MAX_FPS;
    mux->min_fps = DEFAULT_MIN_FPS;
    mux->sync_inputs = DEFAULT_SYNC_INPUTS;
    mux->max_latency = DEFAULT_MAX_LATENCY;
    mux->frame_duration = DEFAULT_FRAME_DURATION;

    mux->num_sink_pads = 0;
    mux->batch_id = 0;
    mux->started = FALSE;
    mux->send_stream_start = TRUE;
    mux->eos_pending = FALSE;
    mux->sinkpads = NULL;
    mux->current_caps = NULL;
    mux->segment_sent = FALSE;
    mux->last_output_time = GST_CLOCK_TIME_NONE;
    mux->min_fps_duration = GST_CLOCK_TIME_NONE;
    mux->max_fps_duration = GST_CLOCK_TIME_NONE;

    g_mutex_init(&mux->lock);
    g_cond_init(&mux->cond);

    gst_segment_init(&mux->segment, GST_FORMAT_TIME);

    /* Create source pad */
    mux->srcpad = gst_pad_new_from_static_template(&gva_streammux_src_template, "src");
    gst_pad_set_query_function(mux->srcpad, GST_DEBUG_FUNCPTR(gst_gva_streammux_src_query));
    gst_pad_set_event_function(mux->srcpad, GST_DEBUG_FUNCPTR(gst_gva_streammux_src_event));
    gst_pad_use_fixed_caps(mux->srcpad);
    gst_element_add_pad(GST_ELEMENT(mux), mux->srcpad);

    /* Create collect pads */
    mux->collect = gst_collect_pads_new();
    gst_collect_pads_set_function(mux->collect, gst_gva_streammux_collected, mux);
    gst_collect_pads_set_event_function(mux->collect, gst_gva_streammux_sink_event, mux);
}

static void gst_gva_streammux_finalize(GObject *object) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(object);

    g_mutex_clear(&mux->lock);
    g_cond_clear(&mux->cond);

    if (mux->current_caps) {
        gst_caps_unref(mux->current_caps);
        mux->current_caps = NULL;
    }

    if (mux->collect) {
        gst_object_unref(mux->collect);
        mux->collect = NULL;
    }

    G_OBJECT_CLASS(gst_gva_streammux_parent_class)->finalize(object);
}

/* Property set/get */
static void gst_gva_streammux_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(object);

    switch (prop_id) {
    case PROP_MAX_SAME_SOURCE_FRAMES:
        mux->max_same_source_frames = g_value_get_uint(value);
        break;
    case PROP_MAX_FPS:
        mux->max_fps = g_value_get_double(value);
        if (mux->max_fps > 0.0) {
            mux->max_fps_duration = (GstClockTime)(GST_SECOND / mux->max_fps);
        } else {
            mux->max_fps_duration = GST_CLOCK_TIME_NONE;
        }
        break;
    case PROP_MIN_FPS:
        mux->min_fps = g_value_get_double(value);
        if (mux->min_fps > 0.0) {
            mux->min_fps_duration = (GstClockTime)(GST_SECOND / mux->min_fps);
        } else {
            mux->min_fps_duration = GST_CLOCK_TIME_NONE;
        }
        break;
    case PROP_SYNC_INPUTS:
        mux->sync_inputs = g_value_get_boolean(value);
        break;
    case PROP_MAX_LATENCY:
        mux->max_latency = g_value_get_uint64(value);
        break;
    case PROP_FRAME_DURATION:
        mux->frame_duration = g_value_get_uint64(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gva_streammux_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(object);

    switch (prop_id) {
    case PROP_MAX_SAME_SOURCE_FRAMES:
        g_value_set_uint(value, mux->max_same_source_frames);
        break;
    case PROP_MAX_FPS:
        g_value_set_double(value, mux->max_fps);
        break;
    case PROP_MIN_FPS:
        g_value_set_double(value, mux->min_fps);
        break;
    case PROP_SYNC_INPUTS:
        g_value_set_boolean(value, mux->sync_inputs);
        break;
    case PROP_MAX_LATENCY:
        g_value_set_uint64(value, mux->max_latency);
        break;
    case PROP_FRAME_DURATION:
        g_value_set_uint64(value, mux->frame_duration);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
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

    sinkpad = gst_pad_new_from_static_template(&gva_streammux_sink_template, name);

    /* Add to collect pads */
    GstCollectData *cdata = gst_collect_pads_add_pad(mux->collect, sinkpad, sizeof(GstCollectData), NULL, TRUE);
    if (!cdata) {
        GST_ERROR_OBJECT(mux, "Failed to add pad %s to collect pads", name);
        gst_object_unref(sinkpad);
        g_free(name);
        g_mutex_unlock(&mux->lock);
        return NULL;
    }

    /* Store pad index in pad's element private data */
    g_object_set_data(G_OBJECT(sinkpad), "pad-index", GUINT_TO_POINTER(pad_index));

    gst_pad_use_fixed_caps(sinkpad);
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

    mux->sinkpads = g_list_remove(mux->sinkpads, pad);
    mux->num_sink_pads--;

    gst_collect_pads_remove_pad(mux->collect, pad);
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
        mux->started = FALSE;
        mux->send_stream_start = TRUE;
        mux->segment_sent = FALSE;
        mux->batch_id = 0;
        mux->eos_pending = FALSE;
        mux->last_output_time = GST_CLOCK_TIME_NONE;
        gst_segment_init(&mux->segment, GST_FORMAT_TIME);
        gst_collect_pads_start(mux->collect);
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
        gst_collect_pads_stop(mux->collect);
        mux->started = FALSE;
        if (mux->current_caps) {
            gst_caps_unref(mux->current_caps);
            mux->current_caps = NULL;
        }
        break;
    case GST_STATE_CHANGE_READY_TO_NULL:
        break;
    default:
        break;
    }

    return ret;
}

/* Sink event handler through collect pads */
static gboolean gst_gva_streammux_sink_event(GstCollectPads *pads, GstCollectData *data, GstEvent *event,
                                              gpointer user_data) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(user_data);
    gboolean ret = TRUE;

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
        GstCaps *caps = NULL;
        gst_event_parse_caps(event, &caps);
        guint pad_index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(data->pad), "pad-index"));
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

        /* Push events in required order: stream-start -> caps -> segment */
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
        /* Consume the segment event; we'll send our own */
        gst_event_unref(event);
        ret = TRUE;
        break;
    }
    default:
        ret = gst_collect_pads_event_default(pads, data, event, FALSE);
        break;
    }

    return ret;
}

/* Source pad query handler */
static gboolean gst_gva_streammux_src_query(GstPad *pad, GstObject *parent, GstQuery *query) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(parent);

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_LATENCY: {
        gboolean live = FALSE;
        GstClockTime min_latency = 0, max_latency = GST_CLOCK_TIME_NONE;

        /* Query all sink pads for latency */
        GList *l;
        for (l = mux->sinkpads; l; l = l->next) {
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

        /* Add our own latency */
        if (mux->max_latency > 0)
            min_latency += mux->max_latency;

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
        /* Forward to all sink pads */
        gboolean ret = TRUE;
        GList *l;
        for (l = mux->sinkpads; l; l = l->next) {
            GstPad *sinkpad = GST_PAD(l->data);
            gst_event_ref(event);
            if (!gst_pad_push_event(sinkpad, event))
                ret = FALSE;
        }
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

/* Main collected callback: called when all sink pads have a buffer */
static GstFlowReturn gst_gva_streammux_collected(GstCollectPads *pads, gpointer user_data) {
    GstGvaStreammux *mux = GST_GVA_STREAMMUX(user_data);
    GstFlowReturn ret = GST_FLOW_OK;
    GSList *collected;
    guint num_sources;

    /* Fallback: send stream-start/segment if CAPS event never arrived (should not happen in normal pipelines) */
    if (G_UNLIKELY(mux->send_stream_start)) {
        gchar *stream_id = g_strdup_printf("gvastreammux/%08x%08x", g_random_int(), g_random_int());
        gst_pad_push_event(mux->srcpad, gst_event_new_stream_start(stream_id));
        g_free(stream_id);
        mux->send_stream_start = FALSE;
        GST_WARNING_OBJECT(mux, "stream-start sent as fallback from collected callback");
    }

    if (G_UNLIKELY(!mux->segment_sent)) {
        gst_segment_init(&mux->segment, GST_FORMAT_TIME);
        gst_pad_push_event(mux->srcpad, gst_event_new_segment(&mux->segment));
        mux->segment_sent = TRUE;
        GST_WARNING_OBJECT(mux, "segment sent as fallback from collected callback");
    }

    /* Count available pads */
    num_sources = 0;
    for (collected = pads->data; collected; collected = g_slist_next(collected)) {
        num_sources++;
    }

    if (num_sources == 0) {
        GST_WARNING_OBJECT(mux, "No source pads available");
        return GST_FLOW_OK;
    }

    /* Apply FPS throttling */
    gst_gva_streammux_apply_fps_throttle(mux);

    /* Round-robin: collect one buffer from each source and push downstream */
    gboolean any_buffer = FALSE;
    for (collected = pads->data; collected; collected = g_slist_next(collected)) {
        GstCollectData *cdata = (GstCollectData *)collected->data;
        GstBuffer *buf = gst_collect_pads_pop(pads, cdata);

        if (!buf) {
            GST_LOG_OBJECT(mux, "No buffer from pad %s", GST_PAD_NAME(cdata->pad));
            continue;
        }
        any_buffer = TRUE;

        guint pad_index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(cdata->pad), "pad-index"));

        /* Make buffer writable to add metadata */
        buf = gst_buffer_make_writable(buf);

        /* Attach source metadata */
        GstGvaStreammuxMeta *meta = gst_buffer_add_gva_streammux_meta(buf, pad_index, mux->batch_id, num_sources);
        if (meta) {
            GST_LOG_OBJECT(mux, "Push buffer from source %u, batch %" G_GUINT64_FORMAT " (pts=%" GST_TIME_FORMAT ")",
                           pad_index, mux->batch_id, GST_TIME_ARGS(GST_BUFFER_PTS(buf)));
        }

        ret = gst_pad_push(mux->srcpad, buf);
        if (ret != GST_FLOW_OK) {
            GST_WARNING_OBJECT(mux, "Push failed for source %u: %s", pad_index, gst_flow_get_name(ret));
            break;
        }
    }

    if (!any_buffer) {
        GST_INFO_OBJECT(mux, "No buffers collected from any source pads");
        gst_pad_push_event(mux->srcpad, gst_event_new_eos());
        return GST_FLOW_EOS;
    }

    gst_gva_streammux_update_output_time(mux);
    mux->batch_id++;

    return ret;
}
