/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvastreamdemux.h"
#include "dlstreamer/gst/metadata/gstgvastreammuxmeta.h"

#include <cstdio>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC(gst_gva_streamdemux_debug);
#define GST_CAT_DEFAULT gst_gva_streamdemux_debug

/* Properties */
enum {
    PROP_0,
    PROP_MAX_FPS,
};

#define DEFAULT_MAX_FPS 0.0

/* Pad templates — same video caps as gvastreammux */
#define STREAMDEMUX_VIDEO_CAPS                                                                                         \
    GST_VIDEO_CAPS_MAKE("{ BGRx, BGRA, BGR, NV12, I420, RGB, RGBA, RGBx }") "; "                                      \
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:VAMemory", "{ NV12 }") "; "                                          \
            GST_VIDEO_CAPS_MAKE_WITH_FEATURES("memory:DMABuf", "{ DMA_DRM }") "; "

static GstStaticPadTemplate gva_streamdemux_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(STREAMDEMUX_VIDEO_CAPS));

static GstStaticPadTemplate gva_streamdemux_src_template =
    GST_STATIC_PAD_TEMPLATE("src_%u", GST_PAD_SRC, GST_PAD_REQUEST, GST_STATIC_CAPS(STREAMDEMUX_VIDEO_CAPS));

/* Forward declarations */
static void gst_gva_streamdemux_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gva_streamdemux_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_gva_streamdemux_finalize(GObject *object);
static GstPad *gst_gva_streamdemux_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *name,
                                                    const GstCaps *caps);
static void gst_gva_streamdemux_release_pad(GstElement *element, GstPad *pad);
static GstStateChangeReturn gst_gva_streamdemux_change_state(GstElement *element, GstStateChange transition);
static GstFlowReturn gst_gva_streamdemux_chain(GstPad *pad, GstObject *parent, GstBuffer *buf);
static gboolean gst_gva_streamdemux_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);
static gboolean gst_gva_streamdemux_sink_query(GstPad *pad, GstObject *parent, GstQuery *query);

G_DEFINE_TYPE(GstGvaStreamdemux, gst_gva_streamdemux, GST_TYPE_ELEMENT);

static void gst_gva_streamdemux_class_init(GstGvaStreamdemuxClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_gva_streamdemux_debug, "gvastreamdemux", 0, "GVA Stream Demuxer");

    gobject_class->set_property = gst_gva_streamdemux_set_property;
    gobject_class->get_property = gst_gva_streamdemux_get_property;
    gobject_class->finalize = gst_gva_streamdemux_finalize;

    element_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_gva_streamdemux_request_new_pad);
    element_class->release_pad = GST_DEBUG_FUNCPTR(gst_gva_streamdemux_release_pad);
    element_class->change_state = GST_DEBUG_FUNCPTR(gst_gva_streamdemux_change_state);

    /* Pad templates */
    gst_element_class_add_static_pad_template(element_class, &gva_streamdemux_sink_template);
    gst_element_class_add_static_pad_template(element_class, &gva_streamdemux_src_template);

    gst_element_class_set_static_metadata(element_class, "GVA Stream Demuxer", "Video/Demuxer",
                                          "Demuxes a single stream into multiple output pads based on "
                                          "GstGvaStreammuxMeta source_id. Must be used with gvastreammux.",
                                          "Intel Corporation");

    /* Properties */
    g_object_class_install_property(
        gobject_class, PROP_MAX_FPS,
        g_param_spec_double("max-fps", "Max FPS",
                            "Maximum output frame rate per source (0 = unlimited). "
                            "Only set this when the video source is a local file. "
                            "Do not set for RTSP or live sources as it may cause pipeline stalls.",
                            0.0, G_MAXDOUBLE, DEFAULT_MAX_FPS,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_gva_streamdemux_init(GstGvaStreamdemux *demux) {
    demux->max_fps = DEFAULT_MAX_FPS;
    demux->num_src_pads = 0;
    demux->validated = FALSE;
    demux->last_output_time = GST_CLOCK_TIME_NONE;
    demux->max_fps_duration = GST_CLOCK_TIME_NONE;

    demux->srcpads = g_ptr_array_new();
    demux->last_batch_ids = g_array_new(FALSE, TRUE, sizeof(guint64));

    g_mutex_init(&demux->lock);

    /* Create sink pad (always) */
    demux->sinkpad = gst_pad_new_from_static_template(&gva_streamdemux_sink_template, "sink");
    gst_pad_set_chain_function(demux->sinkpad, GST_DEBUG_FUNCPTR(gst_gva_streamdemux_chain));
    gst_pad_set_event_function(demux->sinkpad, GST_DEBUG_FUNCPTR(gst_gva_streamdemux_sink_event));
    gst_pad_set_query_function(demux->sinkpad, GST_DEBUG_FUNCPTR(gst_gva_streamdemux_sink_query));
    gst_element_add_pad(GST_ELEMENT(demux), demux->sinkpad);
}

static void gst_gva_streamdemux_finalize(GObject *object) {
    GstGvaStreamdemux *demux = GST_GVA_STREAMDEMUX(object);

    g_mutex_clear(&demux->lock);
    g_ptr_array_free(demux->srcpads, TRUE);
    g_array_free(demux->last_batch_ids, TRUE);

    G_OBJECT_CLASS(gst_gva_streamdemux_parent_class)->finalize(object);
}

/* Property set/get */
static void gst_gva_streamdemux_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaStreamdemux *demux = GST_GVA_STREAMDEMUX(object);

    switch (prop_id) {
    case PROP_MAX_FPS:
        demux->max_fps = g_value_get_double(value);
        if (demux->max_fps > 0.0) {
            demux->max_fps_duration = (GstClockTime)(GST_SECOND / demux->max_fps);
        } else {
            demux->max_fps_duration = GST_CLOCK_TIME_NONE;
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gva_streamdemux_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaStreamdemux *demux = GST_GVA_STREAMDEMUX(object);

    switch (prop_id) {
    case PROP_MAX_FPS:
        g_value_set_double(value, demux->max_fps);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* Request pad creation: pipeline requests src_0, src_1, etc. */
static GstPad *gst_gva_streamdemux_request_new_pad(GstElement *element, GstPadTemplate *templ, const gchar *req_name,
                                                    const GstCaps *caps) {
    (void)caps;
    (void)templ;
    GstGvaStreamdemux *demux = GST_GVA_STREAMDEMUX(element);
    GstPad *srcpad;
    gchar *name;
    guint pad_index;

    g_mutex_lock(&demux->lock);

    if (req_name && sscanf(req_name, "src_%u", &pad_index) == 1) {
        name = g_strdup(req_name);
    } else {
        pad_index = demux->num_src_pads;
        name = g_strdup_printf("src_%u", pad_index);
    }

    srcpad = gst_pad_new_from_static_template(&gva_streamdemux_src_template, name);
    gst_pad_use_fixed_caps(srcpad);
    gst_element_add_pad(element, srcpad);

    /* Ensure srcpads array can hold this index */
    while (demux->srcpads->len <= pad_index)
        g_ptr_array_add(demux->srcpads, NULL);
    demux->srcpads->pdata[pad_index] = srcpad;

    /* Ensure last_batch_ids array can hold this index */
    while (demux->last_batch_ids->len <= pad_index) {
        guint64 init_val = 0;
        g_array_append_val(demux->last_batch_ids, init_val);
    }

    demux->num_src_pads++;

    GST_INFO_OBJECT(demux, "Created src pad %s (index=%u), total src pads=%u", name, pad_index, demux->num_src_pads);

    g_free(name);
    g_mutex_unlock(&demux->lock);

    return srcpad;
}

static void gst_gva_streamdemux_release_pad(GstElement *element, GstPad *pad) {
    GstGvaStreamdemux *demux = GST_GVA_STREAMDEMUX(element);

    g_mutex_lock(&demux->lock);

    /* Find and remove from srcpads array */
    for (guint i = 0; i < demux->srcpads->len; i++) {
        if (g_ptr_array_index(demux->srcpads, i) == pad) {
            demux->srcpads->pdata[i] = NULL;
            break;
        }
    }
    demux->num_src_pads--;

    gst_element_remove_pad(element, pad);

    GST_INFO_OBJECT(demux, "Released pad, remaining src pads=%u", demux->num_src_pads);

    g_mutex_unlock(&demux->lock);
}

/* State changes */
static GstStateChangeReturn gst_gva_streamdemux_change_state(GstElement *element, GstStateChange transition) {
    GstGvaStreamdemux *demux = GST_GVA_STREAMDEMUX(element);
    GstStateChangeReturn ret;

    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        demux->validated = FALSE;
        demux->last_output_time = GST_CLOCK_TIME_NONE;
        /* Reset batch tracking */
        for (guint i = 0; i < demux->last_batch_ids->len; i++)
            g_array_index(demux->last_batch_ids, guint64, i) = 0;
        break;
    default:
        break;
    }

    ret = GST_ELEMENT_CLASS(gst_gva_streamdemux_parent_class)->change_state(element, transition);

    return ret;
}

/* Apply max-fps throttling */
static void gst_gva_streamdemux_apply_fps_throttle(GstGvaStreamdemux *demux) {
    if (!GST_CLOCK_TIME_IS_VALID(demux->max_fps_duration))
        return;

    GstClock *clock = gst_element_get_clock(GST_ELEMENT(demux));
    if (!clock)
        return;

    GstClockTime now = gst_clock_get_time(clock);
    gst_object_unref(clock);

    if (GST_CLOCK_TIME_IS_VALID(demux->last_output_time)) {
        GstClockTime elapsed = now - demux->last_output_time;
        if (elapsed < demux->max_fps_duration) {
            GstClockTime wait = demux->max_fps_duration - elapsed;
            GST_LOG_OBJECT(demux, "FPS throttle: waiting %" GST_TIME_FORMAT, GST_TIME_ARGS(wait));
            g_usleep(GST_TIME_AS_USECONDS(wait));
        }
    }
}

static void gst_gva_streamdemux_update_output_time(GstGvaStreamdemux *demux) {
    GstClock *clock = gst_element_get_clock(GST_ELEMENT(demux));
    if (clock) {
        demux->last_output_time = gst_clock_get_time(clock);
        gst_object_unref(clock);
    }
}

/* Sink event handler */
static gboolean gst_gva_streamdemux_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    (void)pad;
    GstGvaStreamdemux *demux = GST_GVA_STREAMDEMUX(parent);

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
        GstCaps *caps = NULL;
        gst_event_parse_caps(event, &caps);
        GST_INFO_OBJECT(demux, "Received caps on sink: %" GST_PTR_FORMAT, caps);

        /* Forward caps to all src pads. Each src pad gets its own stream-start, caps, segment. */
        g_mutex_lock(&demux->lock);
        for (guint i = 0; i < demux->srcpads->len; i++) {
            GstPad *srcpad = (GstPad *)g_ptr_array_index(demux->srcpads, i);
            if (!srcpad)
                continue;

            /* Send stream-start */
            gchar *stream_id = g_strdup_printf("gvastreamdemux/src_%u/%08x", i, g_random_int());
            gst_pad_push_event(srcpad, gst_event_new_stream_start(stream_id));
            g_free(stream_id);

            /* Send caps */
            gst_pad_push_event(srcpad, gst_event_new_caps(caps));

            /* Send segment */
            GstSegment segment;
            gst_segment_init(&segment, GST_FORMAT_TIME);
            gst_pad_push_event(srcpad, gst_event_new_segment(&segment));

            GST_INFO_OBJECT(demux, "Sent stream-start/caps/segment to src_%u", i);
        }
        g_mutex_unlock(&demux->lock);

        gst_event_unref(event);
        return TRUE;
    }
    case GST_EVENT_EOS: {
        GST_INFO_OBJECT(demux, "Received EOS, forwarding to all src pads");
        g_mutex_lock(&demux->lock);
        for (guint i = 0; i < demux->srcpads->len; i++) {
            GstPad *srcpad = (GstPad *)g_ptr_array_index(demux->srcpads, i);
            if (srcpad)
                gst_pad_push_event(srcpad, gst_event_new_eos());
        }
        g_mutex_unlock(&demux->lock);
        gst_event_unref(event);
        return TRUE;
    }
    case GST_EVENT_SEGMENT: {
        /* Consume: we send our own segments per src pad in CAPS handler */
        gst_event_unref(event);
        return TRUE;
    }
    case GST_EVENT_STREAM_START: {
        /* Consume: we send our own stream-start per src pad */
        gst_event_unref(event);
        return TRUE;
    }
    default:
        /* Forward other events to all src pads */
        g_mutex_lock(&demux->lock);
        for (guint i = 0; i < demux->srcpads->len; i++) {
            GstPad *srcpad = (GstPad *)g_ptr_array_index(demux->srcpads, i);
            if (srcpad) {
                gst_event_ref(event);
                gst_pad_push_event(srcpad, event);
            }
        }
        g_mutex_unlock(&demux->lock);
        gst_event_unref(event);
        return TRUE;
    }
}

/* Sink query handler */
static gboolean gst_gva_streamdemux_sink_query(GstPad *pad, GstObject *parent, GstQuery *query) {
    GstGvaStreamdemux *demux = GST_GVA_STREAMDEMUX(parent);

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_CAPS: {
        GstCaps *filter;
        gst_query_parse_caps(query, &filter);
        GstCaps *caps = gst_pad_get_pad_template_caps(pad);
        if (filter) {
            GstCaps *result = gst_caps_intersect(caps, filter);
            gst_caps_unref(caps);
            caps = result;
        }
        gst_query_set_caps_result(query, caps);
        gst_caps_unref(caps);
        return TRUE;
    }
    case GST_QUERY_LATENCY: {
        /* Forward latency query to the first active src pad's peer */
        gboolean result = FALSE;
        g_mutex_lock(&demux->lock);
        for (guint i = 0; i < demux->srcpads->len; i++) {
            GstPad *srcpad = (GstPad *)g_ptr_array_index(demux->srcpads, i);
            if (srcpad && gst_pad_is_linked(srcpad)) {
                result = gst_pad_peer_query(srcpad, query);
                break;
            }
        }
        g_mutex_unlock(&demux->lock);
        return result;
    }
    default:
        return gst_pad_query_default(pad, parent, query);
    }
}

/* Main chain function: route buffer to correct src pad based on metadata */
static GstFlowReturn gst_gva_streamdemux_chain(GstPad *pad, GstObject *parent, GstBuffer *buf) {
    (void)pad;
    GstGvaStreamdemux *demux = GST_GVA_STREAMDEMUX(parent);

    /* Read metadata */
    GstGvaStreammuxMeta *meta = gst_buffer_get_gva_streammux_meta(buf);
    if (!meta) {
        GST_ERROR_OBJECT(demux, "Buffer has no GstGvaStreammuxMeta. "
                                "gvastreamdemux must be used with gvastreammux.");
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    guint source_id = meta->source_id;
    guint64 batch_id = meta->batch_id;
    guint num_sources = meta->num_sources;

    /* Validate on first buffer */
    if (G_UNLIKELY(!demux->validated)) {
        g_mutex_lock(&demux->lock);
        if (!demux->validated) {
            if (demux->num_src_pads != num_sources) {
                GST_ERROR_OBJECT(demux,
                                 "Mismatch: gvastreamdemux has %u src pads but gvastreammux reports %u sources. "
                                 "Ensure the same number of src pads are requested.",
                                 demux->num_src_pads, num_sources);
                g_mutex_unlock(&demux->lock);
                gst_buffer_unref(buf);
                return GST_FLOW_ERROR;
            }
            demux->validated = TRUE;
            GST_INFO_OBJECT(demux, "Validated: %u src pads match %u sources from gvastreammux", demux->num_src_pads,
                            num_sources);
        }
        g_mutex_unlock(&demux->lock);
    }

    /* Check source_id is in range */
    if (G_UNLIKELY(source_id >= demux->srcpads->len)) {
        GST_ERROR_OBJECT(demux, "source_id %u out of range (have %u src pads)", source_id, demux->srcpads->len);
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    GstPad *srcpad = (GstPad *)g_ptr_array_index(demux->srcpads, source_id);
    if (G_UNLIKELY(!srcpad)) {
        GST_ERROR_OBJECT(demux, "No src pad for source_id %u", source_id);
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    /* Check batch_id ordering: must be monotonically non-decreasing per src pad */
    guint64 *last_bid = &g_array_index(demux->last_batch_ids, guint64, source_id);
    if (G_UNLIKELY(batch_id < *last_bid)) {
        GST_WARNING_OBJECT(demux,
                           "Out-of-order batch_id on src_%u: got %" G_GUINT64_FORMAT " but last was %" G_GUINT64_FORMAT,
                           source_id, batch_id, *last_bid);
    }
    *last_bid = batch_id;

    /* Apply FPS throttling (global across all src pads) */
    gst_gva_streamdemux_apply_fps_throttle(demux);

    GST_LOG_OBJECT(demux, "Routing buffer to src_%u (batch %" G_GUINT64_FORMAT ", pts=%" GST_TIME_FORMAT ")", source_id,
                   batch_id, GST_TIME_ARGS(GST_BUFFER_PTS(buf)));

    GstFlowReturn ret = gst_pad_push(srcpad, buf);

    gst_gva_streamdemux_update_output_time(demux);

    if (ret != GST_FLOW_OK && ret != GST_FLOW_FLUSHING) {
        GST_WARNING_OBJECT(demux, "Push to src_%u failed: %s", source_id, gst_flow_get_name(ret));
    }

    return ret;
}
