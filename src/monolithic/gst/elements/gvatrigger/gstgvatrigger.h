/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_GVA_TRIGGER_H__
#define __GST_GVA_TRIGGER_H__

#include <gst/codecparsers/gsth264parser.h>
#include <gst/codecparsers/gsth265parser.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_TRIGGER (gst_gva_trigger_get_type())
#define GST_GVA_TRIGGER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_TRIGGER, GstGvaTrigger))
#define GST_GVA_TRIGGER_CLASS(klass)                                                                                    \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_TRIGGER, GstGvaTriggerClass))
#define GST_IS_GVA_TRIGGER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_TRIGGER))
#define GST_IS_GVA_TRIGGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_TRIGGER))

/* Number of leading non-IDR access units used to seed the EMA baseline. */
#define GST_GVA_TRIGGER_WARMUP_COUNT 60

typedef struct _GstGvaTrigger GstGvaTrigger;
typedef struct _GstGvaTriggerClass GstGvaTriggerClass;

typedef enum {
    GST_GVA_TRIGGER_CODEC_H264,
    GST_GVA_TRIGGER_CODEC_H265,
} GstGvaTriggerCodec;

typedef enum {
    GST_GVA_TRIGGER_FORMAT_BYTE_STREAM,
    GST_GVA_TRIGGER_FORMAT_PACKETIZED, /* avc / hvc1 / hev1: length-prefixed NAL units */
} GstGvaTriggerStreamFormat;

/**
 * _GstGvaTrigger:
 *
 * Coded-size motion gate for H.264/H.265 access units (one buffer == one
 * access unit, alignment=au). Reads only NAL unit sizes, no entropy decoding,
 * to tell quiet access units apart from ones with motion, and caches
 * access units from the start of each GOP until either motion is detected
 * (the cache is flushed and the rest of the GOP is forwarded live, sticky
 * until the next IDR) or the next IDR arrives (the cache is discarded).
 *
 * Properties:
 *  - k: motion threshold multiplier (size > k * baseline -> motion)
 *  - alpha: EMA update rate, applied only to quiet, non-IDR access units
 *  - margin: minimum absolute byte margin added to the threshold
 *  - max-cache-time-ms: upper bound on how much quiet video is buffered
 *  - min-baseline: floor for the EMA baseline, in bytes
 */
struct _GstGvaTrigger {
    GstElement element;

    GstPad *sinkpad;
    GstPad *srcpad;

    /* Properties, guarded by GST_OBJECT_LOCK */
    gdouble k;
    gdouble alpha;
    guint64 margin;
    GstClockTime max_cache_time;
    guint64 min_baseline;

    /* Everything below is only touched under the sink pad's stream lock or while stopped. */

    /* Negotiated input format */
    GstGvaTriggerCodec codec;
    GstGvaTriggerStreamFormat stream_format;
    guint nal_length_size;

    /* NAL unit boundary/type scanning, via gstreamer-codecparsers */
    GstH264NalParser *h264_parser;
    GstH265Parser *h265_parser;

    /* Gating state */
    gdouble ema;
    gboolean ema_initialized;
    guint64 warmup_sum;
    guint warmup_count;
    gboolean forwarding; /* TRUE = passing buffers live (sticky until next IDR once motion fires), FALSE = caching */

    /* Access units cached since the start of the current GOP */
    GQueue *gop_buffer;
};

struct _GstGvaTriggerClass {
    GstElementClass parent_class;
};

GType gst_gva_trigger_get_type(void);

G_END_DECLS

#endif /* __GST_GVA_TRIGGER_H__ */
