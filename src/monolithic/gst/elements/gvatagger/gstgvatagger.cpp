/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvatagger.h"

GST_DEBUG_CATEGORY_STATIC(gst_gva_tagger_debug);
#define GST_CAT_DEFAULT gst_gva_tagger_debug

enum {
    PROP_0,
    PROP_K,
    PROP_ALPHA,
    PROP_MARGIN,
    PROP_MIN_BASELINE,
};

/* Tuned for recall: missed motion costs more than an extra inference. */
#define DEFAULT_K 1.5
#define DEFAULT_ALPHA 0.03
#define DEFAULT_MARGIN 8
#define DEFAULT_MIN_BASELINE 10

#define TAGGER_CAPS                                                                                                    \
    "video/x-h264, stream-format=(string){ byte-stream, avc }, alignment=(string)au; "                                 \
    "video/x-h265, stream-format=(string){ byte-stream, hvc1, hev1 }, alignment=(string)au"

static GstStaticPadTemplate gva_tagger_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(TAGGER_CAPS));
static GstStaticPadTemplate gva_tagger_src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(TAGGER_CAPS));

static void gst_gva_tagger_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gva_tagger_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_gva_tagger_finalize(GObject *object);
static gboolean gst_gva_tagger_start(GstBaseTransform *trans);
static gboolean gst_gva_tagger_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps);
static gboolean gst_gva_tagger_sink_event(GstBaseTransform *trans, GstEvent *event);
static GstFlowReturn gst_gva_tagger_transform_ip(GstBaseTransform *trans, GstBuffer *buf);

G_DEFINE_TYPE(GstGvaTagger, gst_gva_tagger, GST_TYPE_BASE_TRANSFORM);

static void gst_gva_tagger_class_init(GstGvaTaggerClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *base_class = GST_BASE_TRANSFORM_CLASS(klass);

    GST_DEBUG_CATEGORY_INIT(gst_gva_tagger_debug, "gvatagger", 0, "GVA coded-size motion tagger");

    /* Tagless custom meta, so GstVideoDecoder-based decoders copy it to decoded frames. */
    gst_meta_register_custom_simple(GST_GVA_TAGGER_META_NAME);

    gobject_class->set_property = gst_gva_tagger_set_property;
    gobject_class->get_property = gst_gva_tagger_get_property;
    gobject_class->finalize = gst_gva_tagger_finalize;

    base_class->start = GST_DEBUG_FUNCPTR(gst_gva_tagger_start);
    base_class->set_caps = GST_DEBUG_FUNCPTR(gst_gva_tagger_set_caps);
    base_class->sink_event = GST_DEBUG_FUNCPTR(gst_gva_tagger_sink_event);
    base_class->transform_ip = GST_DEBUG_FUNCPTR(gst_gva_tagger_transform_ip);

    gst_element_class_add_static_pad_template(element_class, &gva_tagger_sink_template);
    gst_element_class_add_static_pad_template(element_class, &gva_tagger_src_template);

    gst_element_class_set_static_metadata(
        element_class, "GVA Coded-Size Tagger", "Video/Filter",
        "Reads only H.264/H.265 NAL unit sizes and tags every access unit with GvaTriggerMeta "
        "(motion, keyframe). Nothing is dropped; the meta survives decoding so "
        "downstream elements can skip inference on quiet frames. No entropy decoding, no pixels.",
        "Intel Corporation");

    g_object_class_install_property(
        gobject_class, PROP_K,
        g_param_spec_double("k", "Threshold multiplier", "Access unit tagged as motion when size > k * baseline", 1.0,
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
        gobject_class, PROP_MIN_BASELINE,
        g_param_spec_uint64("min-baseline", "Minimum baseline",
                            "Floor for the EMA baseline, in bytes, so it can never collapse to ~0", 1,
                            G_MAXUINT64, DEFAULT_MIN_BASELINE,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_gva_tagger_reset(GstGvaTagger *self) {
    self->warmup_sum = 0;
    self->warmup_count = 0;
    self->ema = 0.0;
    self->ema_initialized = FALSE;
}

static void gst_gva_tagger_init(GstGvaTagger *self) {
    self->k = DEFAULT_K;
    self->alpha = DEFAULT_ALPHA;
    self->margin = DEFAULT_MARGIN;
    self->min_baseline = DEFAULT_MIN_BASELINE;

    self->codec = GST_GVA_TAGGER_CODEC_H264;
    self->stream_format = GST_GVA_TAGGER_FORMAT_BYTE_STREAM;
    self->nal_length_size = 4;

    self->h264_parser = gst_h264_nal_parser_new();
    self->h265_parser = gst_h265_parser_new();

    gst_gva_tagger_reset(self);

    /* Only metadata is added, the payload is never touched. */
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
}

static void gst_gva_tagger_finalize(GObject *object) {
    GstGvaTagger *self = GST_GVA_TAGGER(object);

    gst_h264_nal_parser_free(self->h264_parser);
    gst_h265_parser_free(self->h265_parser);

    G_OBJECT_CLASS(gst_gva_tagger_parent_class)->finalize(object);
}

static void gst_gva_tagger_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaTagger *self = GST_GVA_TAGGER(object);

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
    case PROP_MIN_BASELINE:
        self->min_baseline = g_value_get_uint64(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
    GST_OBJECT_UNLOCK(self);
}

static void gst_gva_tagger_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaTagger *self = GST_GVA_TAGGER(object);

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
    case PROP_MIN_BASELINE:
        g_value_set_uint64(value, self->min_baseline);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
    GST_OBJECT_UNLOCK(self);
}

static gboolean gst_gva_tagger_start(GstBaseTransform *trans) {
    gst_gva_tagger_reset(GST_GVA_TAGGER(trans));
    return TRUE;
}

static gboolean gst_gva_tagger_set_caps(GstBaseTransform *trans, GstCaps *incaps, GstCaps *outcaps) {
    GstGvaTagger *self = GST_GVA_TAGGER(trans);
    (void)outcaps;

    GstStructure *s = gst_caps_get_structure(incaps, 0);
    const gchar *name = gst_structure_get_name(s);
    const gchar *stream_format = gst_structure_get_string(s, "stream-format");

    if (g_str_equal(name, "video/x-h264"))
        self->codec = GST_GVA_TAGGER_CODEC_H264;
    else if (g_str_equal(name, "video/x-h265"))
        self->codec = GST_GVA_TAGGER_CODEC_H265;
    else {
        GST_ERROR_OBJECT(self, "Unsupported caps: %" GST_PTR_FORMAT, incaps);
        return FALSE;
    }

    self->nal_length_size = 4;
    if (stream_format &&
        (g_str_equal(stream_format, "avc") || g_str_equal(stream_format, "hvc1") || g_str_equal(stream_format, "hev1"))) {
        self->stream_format = GST_GVA_TAGGER_FORMAT_PACKETIZED;
        if (self->codec == GST_GVA_TAGGER_CODEC_H264) {
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
        self->stream_format = GST_GVA_TAGGER_FORMAT_BYTE_STREAM;
    }

    GST_INFO_OBJECT(self, "Negotiated %s, stream-format=%s, nal-length-size=%u",
                    self->codec == GST_GVA_TAGGER_CODEC_H264 ? "H.264" : "H.265",
                    stream_format ? stream_format : "byte-stream", self->nal_length_size);
    return TRUE;
}

static gboolean gst_gva_tagger_sink_event(GstBaseTransform *trans, GstEvent *event) {
    /* Serialized, so the streaming thread is idle. */
    if (GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_STOP)
        gst_gva_tagger_reset(GST_GVA_TAGGER(trans));

    return GST_BASE_TRANSFORM_CLASS(gst_gva_tagger_parent_class)->sink_event(trans, event);
}

/* NAL unit scanning via gstreamer-codecparsers: counts only VCL (picture
 * data) bytes and flags IDR access units. No slice-header parsing, no
 * entropy decoding, only NAL boundaries and the type byte are read. */

static gboolean gst_gva_tagger_scan_h264(GstGvaTagger *self, const guint8 *data, gsize size, guint64 *vcl_bytes,
                                          gboolean *is_idr, gboolean *has_vcl) {
    guint offset = 0;
    while (offset < size) {
        GstH264NalUnit nalu;
        GstH264ParserResult res = (self->stream_format == GST_GVA_TAGGER_FORMAT_BYTE_STREAM)
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

static gboolean gst_gva_tagger_scan_h265(GstGvaTagger *self, const guint8 *data, gsize size, guint64 *vcl_bytes,
                                          gboolean *is_idr, gboolean *has_vcl) {
    guint offset = 0;
    while (offset < size) {
        GstH265NalUnit nalu;
        GstH265ParserResult res = (self->stream_format == GST_GVA_TAGGER_FORMAT_BYTE_STREAM)
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

static void gst_gva_tagger_scan_nals(GstGvaTagger *self, const guint8 *data, gsize size, guint64 *vcl_bytes,
                                     gboolean *is_idr, gboolean *has_vcl) {
    *vcl_bytes = 0;
    *is_idr = FALSE;
    *has_vcl = FALSE;

    gboolean ok = (self->codec == GST_GVA_TAGGER_CODEC_H264)
                     ? gst_gva_tagger_scan_h264(self, data, size, vcl_bytes, is_idr, has_vcl)
                     : gst_gva_tagger_scan_h265(self, data, size, vcl_bytes, is_idr, has_vcl);
    if (!ok)
        GST_WARNING_OBJECT(self, "Failed to fully parse access unit NAL units; using partial size sum");
}

/* gst_gva_tagger_transform_ip: Read H.264/H.265 NAL unit sizes to detect motion and attach metadata.
 *
 * This function is called for every access unit (AU) in the H.264/H.265 bitstream.
 * It reads only NAL sizes (no entropy decoding, no pixel access), maintains a baseline
 * of quiet frame sizes using exponential moving average (EMA), and flags each AU as
 * motion or quiet. The result is attached as GvaTriggerMeta, which survives decoding
 * so downstream inference can skip quiet frames.
 *
 * Warm-up phase:
 *   - First GST_GVA_TAGGER_WARMUP_COUNT quiet P-frames establish a baseline
 *   - I-frames (IDRs) are skipped during warm-up (they're naturally large)
 *   - During warm-up, all frames are tagged as motion=TRUE (fail-safe)
 *
 * Steady state:
 *   - I-frames are always tagged as motion=FALSE (keyframes, not affected by content)
 *   - P-frames: motion = vcl_bytes > MAX(k * baseline, baseline + margin)
 *   - Quiet P-frames: baseline is updated via EMA to track slow content changes
 *   - Motion P-frames: baseline is frozen (don't learn from motion)
 */
static GstFlowReturn gst_gva_tagger_transform_ip(GstBaseTransform *trans, GstBuffer *buf) {
    GstGvaTagger *self = GST_GVA_TAGGER(trans);

    /* Read properties under lock; release before expensive operations to avoid blocking. */
    GST_OBJECT_LOCK(self);
    const gdouble k = self->k;
    const gdouble alpha = self->alpha;
    const gdouble margin = (gdouble)self->margin;
    const gdouble min_baseline = (gdouble)self->min_baseline;
    GST_OBJECT_UNLOCK(self);

    /* Map buffer to read NAL data (read-only). */
    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READ)) {
        GST_ELEMENT_ERROR(self, RESOURCE, READ, ("Failed to map input buffer"), (NULL));
        return GST_FLOW_ERROR;
    }

    /* Scan NAL units to extract VCL (picture data) size, detect IDR, and check for VCL presence. */
    guint64 vcl_bytes = 0;
    gboolean is_idr = FALSE;
    gboolean has_vcl = FALSE;
    gst_gva_tagger_scan_nals(self, map.data, map.size, &vcl_bytes, &is_idr, &has_vcl);
    gst_buffer_unmap(buf, &map);

    /* Determine motion flag for this AU. */
    gboolean motion;
    if (!self->ema_initialized) {
        /* Warm-up: collect quiet P-frame sizes to establish baseline. */
        if (has_vcl && !is_idr) {
            self->warmup_sum += vcl_bytes;
            if (++self->warmup_count == GST_GVA_TAGGER_WARMUP_COUNT) {
                /* Warm-up complete: initialize baseline from average of quiet P-frames. */
                self->ema = MAX((gdouble)self->warmup_sum / GST_GVA_TAGGER_WARMUP_COUNT, min_baseline);
                self->ema_initialized = TRUE;
                GST_INFO_OBJECT(self, "Warm-up complete, initial baseline=%.1f bytes", self->ema);
            }
        }
        motion = TRUE; /* Baseline not ready yet: fail-safe (tag everything as motion). */
    } else if (is_idr || !has_vcl) {
        /* I-frames and AU with no VCL data are always tagged as non-motion
         * (I-frames are large regardless of content, no VCL = no picture data). */
        motion = FALSE;
    } else {
        /* Steady state P-frame: compare size against motion threshold. */
        motion = (gdouble)vcl_bytes > MAX(k * self->ema, self->ema + margin);
        if (!motion) {
            /* Quiet frame: update baseline using EMA. This learns scene brightness/complexity changes. */
            self->ema = MAX(alpha * (gdouble)vcl_bytes + (1.0 - alpha) * self->ema, min_baseline);
        }
        /* Motion frame: freeze baseline (don't learn from transient content). */
    }

    /* Attach motion tag to buffer as GvaTriggerMeta custom metadata. */
    GstCustomMeta *meta = gst_buffer_add_custom_meta(buf, GST_GVA_TAGGER_META_NAME);
    if (!meta) {
        GST_WARNING_OBJECT(self, "Failed to attach %s", GST_GVA_TAGGER_META_NAME);
        return GST_FLOW_OK;
    }
    gst_structure_set(gst_custom_meta_get_structure(meta), "motion", G_TYPE_BOOLEAN, motion, "keyframe",
                      G_TYPE_BOOLEAN, is_idr, NULL);

    /* Log for debugging. */
    GST_LOG_OBJECT(self, "pts=%" GST_TIME_FORMAT " size=%" G_GUINT64_FORMAT " baseline=%.1f keyframe=%d motion=%d",
                   GST_TIME_ARGS(GST_BUFFER_PTS(buf)), vcl_bytes, self->ema, is_idr, motion);
    return GST_FLOW_OK;
}
