/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvatrigger.h"

#include <stdlib.h>

GST_DEBUG_CATEGORY_STATIC(gst_gva_trigger_debug);
#define GST_CAT_DEFAULT gst_gva_trigger_debug

/* Safety cap on cached access units regardless of max-cache-time, in case
 * timestamps are missing and the time-based trim cannot run. */
#define GST_GVA_TRIGGER_MAX_CACHE_BUFFERS 3000

enum {
    PROP_0,
    PROP_K,
    PROP_ALPHA,
    PROP_MARGIN,
    PROP_MAX_CACHE_TIME_MS,
    PROP_MIN_BASELINE,
};

#define DEFAULT_K 3.0
#define DEFAULT_ALPHA 0.03
#define DEFAULT_MARGIN 64
#define DEFAULT_MAX_CACHE_TIME_MS 4000
#define DEFAULT_MIN_BASELINE 100

#define TRIGGER_CAPS                                                                                                   \
    "video/x-h264, stream-format=(string){ byte-stream, avc }, alignment=(string)au; "                                 \
    "video/x-h265, stream-format=(string){ byte-stream, hvc1, hev1 }, alignment=(string)au"

static GstStaticPadTemplate gva_trigger_gop_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(TRIGGER_CAPS));
static GstStaticPadTemplate gva_trigger_gop_src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(TRIGGER_CAPS));

static void gst_gva_trigger_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gva_trigger_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_gva_trigger_finalize(GObject *object);
static GstStateChangeReturn gst_gva_trigger_change_state(GstElement *element, GstStateChange transition);
static GstFlowReturn gst_gva_trigger_chain(GstPad *pad, GstObject *parent, GstBuffer *buf);
static gboolean gst_gva_trigger_sink_event(GstPad *pad, GstObject *parent, GstEvent *event);
static gboolean gst_gva_trigger_sink_query(GstPad *pad, GstObject *parent, GstQuery *query);
static gboolean gst_gva_trigger_src_event(GstPad *pad, GstObject *parent, GstEvent *event);
static gboolean gst_gva_trigger_src_query(GstPad *pad, GstObject *parent, GstQuery *query);

G_DEFINE_TYPE(GstGvaTrigger, gst_gva_trigger, GST_TYPE_ELEMENT);

static void gst_gva_trigger_class_init(GstGvaTriggerClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_gva_trigger_debug, "gvatrigger", 0, "GVA coded-size motion GOP gate");

    gobject_class->set_property = gst_gva_trigger_set_property;
    gobject_class->get_property = gst_gva_trigger_get_property;
    gobject_class->finalize = gst_gva_trigger_finalize;

    element_class->change_state = GST_DEBUG_FUNCPTR(gst_gva_trigger_change_state);

    gst_element_class_add_static_pad_template(element_class, &gva_trigger_gop_sink_template);
    gst_element_class_add_static_pad_template(element_class, &gva_trigger_gop_src_template);

    gst_element_class_set_static_metadata(
        element_class, "GVA Coded-Size GOP Gate", "Video/Filter",
        "Reads only H.264/H.265 NAL unit sizes to tell quiet access units apart from motion. "
        "Caches access units from the start of each GOP; discards the cache if nothing ever "
        "triggered before the next IDR, and flushes it the instant motion is detected, after "
        "which the rest of that GOP is forwarded live. No entropy decoding, no pixels.",
        "Intel Corporation");

    g_object_class_install_property(
        gobject_class, PROP_K,
        g_param_spec_double("k", "Threshold multiplier", "Access unit flagged as motion when size > k * baseline", 1.0,
                            G_MAXDOUBLE, DEFAULT_K, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_ALPHA,
        g_param_spec_double("alpha", "EMA rate", "Baseline update rate, applied only to quiet, non-IDR access units",
                            0.0, 1.0, DEFAULT_ALPHA, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_MARGIN,
        g_param_spec_uint64("margin", "Threshold margin",
                            "Minimum absolute byte margin added on top of k * baseline", 0, G_MAXUINT64,
                            DEFAULT_MARGIN, (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_MAX_CACHE_TIME_MS,
        g_param_spec_uint("max-cache-time-ms", "Max cache time (ms)",
                          "Upper bound on how much quiet video is buffered before the oldest cached "
                          "access units are permanently dropped",
                          0, G_MAXUINT, DEFAULT_MAX_CACHE_TIME_MS,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject_class, PROP_MIN_BASELINE,
        g_param_spec_uint64("min-baseline", "Minimum baseline",
                            "Floor for the EMA baseline, in bytes, so it can never collapse to ~0", 1,
                            G_MAXUINT64, DEFAULT_MIN_BASELINE,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_gva_trigger_init(GstGvaTrigger *self) {
    self->k = DEFAULT_K;
    self->alpha = DEFAULT_ALPHA;
    self->margin = DEFAULT_MARGIN;
    self->max_cache_time = DEFAULT_MAX_CACHE_TIME_MS * GST_MSECOND;
    self->min_baseline = DEFAULT_MIN_BASELINE;

    self->codec = GST_GVA_TRIGGER_CODEC_H264;
    self->stream_format = GST_GVA_TRIGGER_FORMAT_BYTE_STREAM;
    self->nal_length_size = 4;

    self->ema = 0.0;
    self->ema_initialized = FALSE;
    self->warmup_sum = 0;
    self->warmup_count = 0;
    /* Pass through during warm-up: the baseline is not known yet. */
    self->forwarding = TRUE;

    self->gop_buffer = g_queue_new();

    self->h264_parser = gst_h264_nal_parser_new();
    self->h265_parser = gst_h265_parser_new();

    self->sinkpad = gst_pad_new_from_static_template(&gva_trigger_gop_sink_template, "sink");
    gst_pad_set_chain_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_gva_trigger_chain));
    gst_pad_set_event_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_gva_trigger_sink_event));
    gst_pad_set_query_function(self->sinkpad, GST_DEBUG_FUNCPTR(gst_gva_trigger_sink_query));
    gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);

    self->srcpad = gst_pad_new_from_static_template(&gva_trigger_gop_src_template, "src");
    gst_pad_set_event_function(self->srcpad, GST_DEBUG_FUNCPTR(gst_gva_trigger_src_event));
    gst_pad_set_query_function(self->srcpad, GST_DEBUG_FUNCPTR(gst_gva_trigger_src_query));
    gst_pad_use_fixed_caps(self->srcpad);
    gst_element_add_pad(GST_ELEMENT(self), self->srcpad);
}

static void gst_gva_trigger_discard_cache(GstGvaTrigger *self) {
    GstBuffer *cached;
    while ((cached = (GstBuffer *)g_queue_pop_head(self->gop_buffer)) != NULL)
        gst_buffer_unref(cached);
}

static void gst_gva_trigger_reset(GstGvaTrigger *self) {
    gst_gva_trigger_discard_cache(self);
    self->forwarding = TRUE;
    self->warmup_sum = 0;
    self->warmup_count = 0;
    self->ema = 0.0;
    self->ema_initialized = FALSE;
}

static void gst_gva_trigger_finalize(GObject *object) {
    GstGvaTrigger *self = GST_GVA_TRIGGER(object);

    gst_gva_trigger_discard_cache(self);
    g_queue_free(self->gop_buffer);
    gst_h264_nal_parser_free(self->h264_parser);
    gst_h265_parser_free(self->h265_parser);

    G_OBJECT_CLASS(gst_gva_trigger_parent_class)->finalize(object);
}

static GstStateChangeReturn gst_gva_trigger_change_state(GstElement *element, GstStateChange transition) {
    GstStateChangeReturn ret = GST_ELEMENT_CLASS(gst_gva_trigger_parent_class)->change_state(element, transition);

    /* Reset after chaining up: pads are deactivated, so the streaming thread has stopped. */
    if (transition == GST_STATE_CHANGE_PAUSED_TO_READY)
        gst_gva_trigger_reset(GST_GVA_TRIGGER(element));

    return ret;
}

static void gst_gva_trigger_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaTrigger *self = GST_GVA_TRIGGER(object);

    GST_OBJECT_LOCK(self);
    switch (prop_id) {
    case PROP_K:
        self->k = g_value_get_double(value);
        break;
    case PROP_ALPHA:
        self->alpha = g_value_get_double(value);
        break;
    case PROP_MARGIN:
        self->margin = g_value_get_uint64(value);
        break;
    case PROP_MAX_CACHE_TIME_MS:
        self->max_cache_time = g_value_get_uint(value) * GST_MSECOND;
        break;
    case PROP_MIN_BASELINE:
        self->min_baseline = g_value_get_uint64(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
    GST_OBJECT_UNLOCK(self);
}

static void gst_gva_trigger_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaTrigger *self = GST_GVA_TRIGGER(object);

    GST_OBJECT_LOCK(self);
    switch (prop_id) {
    case PROP_K:
        g_value_set_double(value, self->k);
        break;
    case PROP_ALPHA:
        g_value_set_double(value, self->alpha);
        break;
    case PROP_MARGIN:
        g_value_set_uint64(value, self->margin);
        break;
    case PROP_MAX_CACHE_TIME_MS:
        g_value_set_uint(value, (guint)(self->max_cache_time / GST_MSECOND));
        break;
    case PROP_MIN_BASELINE:
        g_value_set_uint64(value, self->min_baseline);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
    GST_OBJECT_UNLOCK(self);
}

/* NAL unit scanning via gstreamer-codecparsers: counts only VCL (picture
 * data) bytes and flags IDR access units. No slice-header parsing, no
 * entropy decoding, only NAL boundaries and the type byte are read. */

static gboolean gst_gva_trigger_scan_h264(GstGvaTrigger *self, const guint8 *data, gsize size,
                                              guint64 *vcl_bytes, gboolean *is_idr, gboolean *has_vcl) {
    guint offset = 0;
    while (offset < size) {
        GstH264NalUnit nalu;
        GstH264ParserResult res = (self->stream_format == GST_GVA_TRIGGER_FORMAT_BYTE_STREAM)
                                      ? gst_h264_parser_identify_nalu(self->h264_parser, data, offset, size, &nalu)
                                      : gst_h264_parser_identify_nalu_avc(self->h264_parser, data, offset, size,
                                                                          (guint8)self->nal_length_size, &nalu);
        if (res != GST_H264_PARSER_OK && res != GST_H264_PARSER_NO_NAL_END)
            return res == GST_H264_PARSER_NO_NAL; /* ran out of NAL units cleanly vs. a parse error */

        if (nalu.type >= GST_H264_NAL_SLICE && nalu.type <= GST_H264_NAL_SLICE_IDR) {
            *vcl_bytes += nalu.size;
            *has_vcl = TRUE;
            *is_idr |= (nalu.type == GST_H264_NAL_SLICE_IDR);
        }

        if (res == GST_H264_PARSER_NO_NAL_END)
            break;
        offset = nalu.offset + nalu.size;
    }
    return TRUE;
}

static gboolean gst_gva_trigger_scan_h265(GstGvaTrigger *self, const guint8 *data, gsize size,
                                              guint64 *vcl_bytes, gboolean *is_idr, gboolean *has_vcl) {
    guint offset = 0;
    while (offset < size) {
        GstH265NalUnit nalu;
        GstH265ParserResult res = (self->stream_format == GST_GVA_TRIGGER_FORMAT_BYTE_STREAM)
                                      ? gst_h265_parser_identify_nalu(self->h265_parser, data, offset, size, &nalu)
                                      : gst_h265_parser_identify_nalu_hevc(self->h265_parser, data, offset, size,
                                                                          (guint8)self->nal_length_size, &nalu);
        if (res != GST_H265_PARSER_OK && res != GST_H265_PARSER_NO_NAL_END)
            return res == GST_H265_PARSER_NO_NAL;

        if (nalu.type <= 31) { /* VCL NAL unit types are 0-31 */
            *vcl_bytes += nalu.size;
            *has_vcl = TRUE;
            *is_idr |= GST_H265_IS_NAL_TYPE_IDR(nalu.type);
        }

        if (res == GST_H265_PARSER_NO_NAL_END)
            break;
        offset = nalu.offset + nalu.size;
    }
    return TRUE;
}

static void gst_gva_trigger_scan_nals(GstGvaTrigger *self, const guint8 *data, gsize size, guint64 *vcl_bytes,
                                         gboolean *is_idr, gboolean *has_vcl) {
    *vcl_bytes = 0;
    *is_idr = FALSE;
    *has_vcl = FALSE;

    gboolean ok = (self->codec == GST_GVA_TRIGGER_CODEC_H264)
                     ? gst_gva_trigger_scan_h264(self, data, size, vcl_bytes, is_idr, has_vcl)
                     : gst_gva_trigger_scan_h265(self, data, size, vcl_bytes, is_idr, has_vcl);
    if (!ok)
        GST_WARNING_OBJECT(self, "Failed to fully parse access unit NAL units; using partial size sum");
}

/* Drop the oldest cached access units once the cache spans more than
 * max-cache-time (or, if timestamps are missing, more than a hard count cap).
 * This permanently loses those access units: it bounds how far back a
 * flush can look, matching a fixed pre-roll window rather than unbounded
 * lookback. */
static void gst_gva_trigger_trim_cache(GstGvaTrigger *self, GstClockTime max_cache_time) {
    while (self->gop_buffer->length > 1) {
        GstBuffer *head = (GstBuffer *)g_queue_peek_head(self->gop_buffer);
        GstBuffer *tail = (GstBuffer *)g_queue_peek_tail(self->gop_buffer);
        GstClockTime span = 0;
        if (GST_BUFFER_PTS_IS_VALID(head) && GST_BUFFER_PTS_IS_VALID(tail) && GST_BUFFER_PTS(tail) >= GST_BUFFER_PTS(head))
            span = GST_BUFFER_PTS(tail) - GST_BUFFER_PTS(head);

        if (span > max_cache_time || self->gop_buffer->length > GST_GVA_TRIGGER_MAX_CACHE_BUFFERS)
            gst_buffer_unref((GstBuffer *)g_queue_pop_head(self->gop_buffer));
        else
            break;
    }
}

static GstFlowReturn gst_gva_trigger_flush_cache(GstGvaTrigger *self) {
    GstFlowReturn ret = GST_FLOW_OK;
    GstBuffer *cached;
    while ((cached = (GstBuffer *)g_queue_pop_head(self->gop_buffer)) != NULL) {
        if (ret == GST_FLOW_OK)
            ret = gst_pad_push(self->srcpad, cached);
        else
            gst_buffer_unref(cached);
    }
    return ret;
}

/* gst_gva_trigger_chain: Implement GOP-level bandwidth gating based on coded-size motion detection.
 *
 * This is the main processing function called for every H.264/H.265 access unit (AU).
 * It implements a GOP (Group of Pictures) buffering strategy:
 *
 * WARM-UP PHASE (first ~30 P-frames):
 *   - All AUs are forwarded live (baseline not yet established)
 *   - Quiet P-frame sizes are accumulated to compute initial baseline
 *   - I-frames (IDRs) are skipped during warm-up
 *
 * STEADY STATE:
 *   - Each IDR (I-frame) starts a new GOP
 *   - P-frames are checked for motion: size > MAX(k * baseline, baseline + margin)?
 *   - If motion detected:
 *     * Flush the buffered "quiet start" frames from the GOP
 *     * Switch to live forwarding (forwarding=TRUE) for the rest of the GOP
 *     * Sticky: a dip below threshold mid-GOP doesn't resume buffering
 *   - If no motion by next IDR:
 *     * Discard all buffered frames (bandwidth reduction achieved)
 *   - Quiet P-frames: baseline is updated via EMA, then frame is buffered
 *
 * BUFFER MANAGEMENT:
 *   - Bounded by max-cache-time-ms: oldest frames are permanently dropped
 *   - Hard cap at GST_GVA_TRIGGER_MAX_CACHE_BUFFERS (3000) for missing timestamps
 *   - EOS and FLUSH_STOP events flush and reset the buffer
 *
 * Result: Quiet GOPs (e.g., static camera, frozen scenes) are entirely dropped,
 * reducing bandwidth. Motion GOPs pass through with minimal latency (one IDR buffer depth).
 */
static GstFlowReturn gst_gva_trigger_chain(GstPad *pad, GstObject *parent, GstBuffer *buf) {
    GstGvaTrigger *self = GST_GVA_TRIGGER(parent);
    (void)pad;

    /* Read motion detection parameters under lock. */
    GST_OBJECT_LOCK(self);
    const gdouble k = self->k;
    const gdouble alpha = self->alpha;
    const gdouble margin = (gdouble)self->margin;
    const gdouble min_baseline = (gdouble)self->min_baseline;
    const GstClockTime max_cache_time = self->max_cache_time;
    GST_OBJECT_UNLOCK(self);

    /* Map and scan NAL units to extract VCL size, IDR flag, VCL presence. */
    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        GST_ERROR_OBJECT(self, "Failed to map input buffer");
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    guint64 vcl_bytes = 0;
    gboolean is_idr = FALSE;
    gboolean has_vcl = FALSE;

    gst_gva_trigger_scan_nals(self, map.data, map.size, &vcl_bytes, &is_idr, &has_vcl);
    gst_buffer_unmap(buf, &map);

    /* I-frame (IDR): marks the start of a new GOP. */
    if (is_idr) {
        /* During warm-up, pass I-frames through (baseline not ready).
         * After warm-up, I-frame begins a new GOP: cache the IDR and wait for motion trigger. */
        if (!self->ema_initialized)
            return gst_pad_push(self->srcpad, buf);
        /* New GOP: discard any quiet frames from the previous GOP that never triggered. */
        gst_gva_trigger_discard_cache(self);
        /* Start buffering the new GOP from its I-frame. */
        self->forwarding = FALSE;
        g_queue_push_tail(self->gop_buffer, buf);
        return GST_FLOW_OK;
    }

    /* P-frame with no VCL: non-standard access unit (e.g., filler), forward without gating. */
    if (!has_vcl) {
        GST_WARNING_OBJECT(self, "Access unit has no VCL NAL units; forwarding without gating");
        /* If we were buffering, flush the buffer to resume live forwarding. */
        if (!self->forwarding) {
            GstFlowReturn flush_ret = gst_gva_trigger_flush_cache(self);
            self->forwarding = TRUE;
            if (flush_ret != GST_FLOW_OK) {
                gst_buffer_unref(buf);
                return flush_ret;
            }
        }
        return gst_pad_push(self->srcpad, buf);
    }

    /* P-frame during warm-up: accumulate size to establish baseline. */
    if (!self->ema_initialized) {
        self->warmup_sum += vcl_bytes;
        self->warmup_count++;
        if (self->warmup_count == GST_GVA_TRIGGER_WARMUP_COUNT) {
            /* Warm-up complete: initialize EMA baseline from collected sizes. */
            self->ema = MAX((gdouble)self->warmup_sum / GST_GVA_TRIGGER_WARMUP_COUNT, min_baseline);
            self->ema_initialized = TRUE;
            GST_INFO_OBJECT(self, "Warm-up complete, initial baseline=%.1f bytes", self->ema);
        }
        /* Warm-up: forward all frames live. */
        self->forwarding = TRUE;
        return gst_pad_push(self->srcpad, buf);
    }

    /* P-frame steady state: check for motion and manage buffering. */
    gdouble threshold = MAX(k * self->ema, self->ema + margin);
    gboolean motion = (gdouble)vcl_bytes > threshold;

    if (motion || self->forwarding) {
        /* Motion detected OR already forwarding: flush buffered frames and forward live.
         * Forwarding is sticky until the next IDR (no resume-to-buffer mid-GOP). */
        if (!self->forwarding) {
            /* First motion frame this GOP: flush the quiet buffer. */
            GstFlowReturn flush_ret = gst_gva_trigger_flush_cache(self);
            if (flush_ret != GST_FLOW_OK) {
                gst_buffer_unref(buf);
                return flush_ret;
            }
        }
        self->forwarding = TRUE;
        return gst_pad_push(self->srcpad, buf);
    }

    /* Quiet P-frame and no motion trigger yet: buffer it, update baseline. */
    self->ema = MAX(alpha * (gdouble)vcl_bytes + (1.0 - alpha) * self->ema, min_baseline);
    g_queue_push_tail(self->gop_buffer, buf);
    /* Prevent unbounded growth: drop oldest frames if cache exceeds time/count bounds. */
    gst_gva_trigger_trim_cache(self, max_cache_time);

    return GST_FLOW_OK;
}

static gboolean gst_gva_trigger_parse_caps(GstGvaTrigger *self, GstCaps *caps) {
    GstStructure *s = gst_caps_get_structure(caps, 0);
    const gchar *name = gst_structure_get_name(s);
    const gchar *stream_format = gst_structure_get_string(s, "stream-format");

    if (g_str_equal(name, "video/x-h264"))
        self->codec = GST_GVA_TRIGGER_CODEC_H264;
    else if (g_str_equal(name, "video/x-h265"))
        self->codec = GST_GVA_TRIGGER_CODEC_H265;
    else {
        GST_ERROR_OBJECT(self, "Unsupported caps: %" GST_PTR_FORMAT, caps);
        return FALSE;
    }

    self->nal_length_size = 4;
    if (stream_format &&
        (g_str_equal(stream_format, "avc") || g_str_equal(stream_format, "hvc1") || g_str_equal(stream_format, "hev1"))) {
        self->stream_format = GST_GVA_TRIGGER_FORMAT_PACKETIZED;
        if (self->codec == GST_GVA_TRIGGER_CODEC_H264) {
            const GValue *codec_data_value = gst_structure_get_value(s, "codec_data");
            GstBuffer *codec_data = codec_data_value ? gst_value_get_buffer(codec_data_value) : NULL;
            GstMapInfo map;
            if (codec_data && gst_buffer_map(codec_data, &map, GST_MAP_READ)) {
                if (map.size > 4)
                    self->nal_length_size = (map.data[4] & 0x03) + 1;
                gst_buffer_unmap(codec_data, &map);
            }
        }
    } else {
        self->stream_format = GST_GVA_TRIGGER_FORMAT_BYTE_STREAM;
    }

    GST_INFO_OBJECT(self, "Negotiated %s, stream-format=%s, nal-length-size=%u",
                    self->codec == GST_GVA_TRIGGER_CODEC_H264 ? "H.264" : "H.265",
                    stream_format ? stream_format : "byte-stream", self->nal_length_size);

    return TRUE;
}

static gboolean gst_gva_trigger_sink_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    GstGvaTrigger *self = GST_GVA_TRIGGER(parent);
    (void)pad;

    switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS: {
        GstCaps *caps;
        gst_event_parse_caps(event, &caps);
        if (!gst_gva_trigger_parse_caps(self, caps)) {
            gst_event_unref(event);
            return FALSE;
        }
        return gst_pad_push_event(self->srcpad, event);
    }
    case GST_EVENT_EOS: {
        GstFlowReturn ret = gst_gva_trigger_flush_cache(self);
        self->forwarding = TRUE;
        if (ret != GST_FLOW_OK && ret != GST_FLOW_FLUSHING)
            GST_WARNING_OBJECT(self, "Failed to flush cache before EOS: %s", gst_flow_get_name(ret));
        return gst_pad_push_event(self->srcpad, event);
    }
    case GST_EVENT_FLUSH_STOP:
        /* Serialized, so the streaming thread is idle; FLUSH_START is not and must not touch state. */
        gst_gva_trigger_reset(self);
        return gst_pad_push_event(self->srcpad, event);
    default:
        return gst_pad_push_event(self->srcpad, event);
    }
}

static gboolean gst_gva_trigger_sink_query(GstPad *pad, GstObject *parent, GstQuery *query) {
    GstGvaTrigger *self = GST_GVA_TRIGGER(parent);
    (void)pad;
    return gst_pad_peer_query(self->srcpad, query);
}

static gboolean gst_gva_trigger_src_event(GstPad *pad, GstObject *parent, GstEvent *event) {
    GstGvaTrigger *self = GST_GVA_TRIGGER(parent);
    (void)pad;
    return gst_pad_push_event(self->sinkpad, event);
}

static gboolean gst_gva_trigger_src_query(GstPad *pad, GstObject *parent, GstQuery *query) {
    GstGvaTrigger *self = GST_GVA_TRIGGER(parent);
    (void)pad;
    return gst_pad_peer_query(self->sinkpad, query);
}
