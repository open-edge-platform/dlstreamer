/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_GVA_TAGGER_H__
#define __GST_GVA_TAGGER_H__

#include <gst/base/gstbasetransform.h>
#include <gst/codecparsers/gsth264parser.h>
#include <gst/codecparsers/gsth265parser.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_TAGGER (gst_gva_tagger_get_type())
#define GST_GVA_TAGGER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_TAGGER, GstGvaTagger))
#define GST_GVA_TAGGER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_TAGGER, GstGvaTaggerClass))
#define GST_IS_GVA_TAGGER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_TAGGER))
#define GST_IS_GVA_TAGGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_TAGGER))

/* GstCustomMeta attached to every access unit. Fields: motion (boolean), keyframe (boolean). */
#define GST_GVA_TAGGER_META_NAME "GvaTriggerMeta"

/* Number of leading non-IDR access units used to seed the EMA baseline. */
#define GST_GVA_TAGGER_WARMUP_COUNT 60

typedef struct _GstGvaTagger GstGvaTagger;
typedef struct _GstGvaTaggerClass GstGvaTaggerClass;

typedef enum {
    GST_GVA_TAGGER_CODEC_H264,
    GST_GVA_TAGGER_CODEC_H265,
} GstGvaTaggerCodec;

typedef enum {
    GST_GVA_TAGGER_FORMAT_BYTE_STREAM,
    GST_GVA_TAGGER_FORMAT_PACKETIZED, /* avc / hvc1 / hev1: length-prefixed NAL units */
} GstGvaTaggerStreamFormat;

/**
 * _GstGvaTagger:
 *
 * Coded-size motion tagger for H.264/H.265 access units (alignment=au).
 * Every access unit passes through unchanged and gets a GvaTriggerMeta saying
 * whether its VCL size exceeds k * EMA baseline. Nothing is dropped or
 * delayed, so the decoder still sees the whole stream; the meta survives
 * GstVideoDecoder-based decoders so downstream elements can skip inference.
 */
struct _GstGvaTagger {
    GstBaseTransform parent;

    /* Properties, guarded by GST_OBJECT_LOCK */
    gdouble k;
    gdouble alpha;
    guint64 margin;
    guint64 min_baseline;

    /* Everything below is only touched from the streaming thread or while stopped. */

    GstGvaTaggerCodec codec;
    GstGvaTaggerStreamFormat stream_format;
    guint nal_length_size;

    GstH264NalParser *h264_parser;
    GstH265Parser *h265_parser;

    gdouble ema;
    gboolean ema_initialized;
    guint64 warmup_sum;
    guint warmup_count;
};

struct _GstGvaTaggerClass {
    GstBaseTransformClass parent_class;
};

GType gst_gva_tagger_get_type(void);

G_END_DECLS

#endif /* __GST_GVA_TAGGER_H__ */
