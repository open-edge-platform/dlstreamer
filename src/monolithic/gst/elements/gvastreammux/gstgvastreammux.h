/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_GVA_STREAMMUX_H__
#define __GST_GVA_STREAMMUX_H__

#include <gst/base/gstcollectpads.h>
#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_STREAMMUX (gst_gva_streammux_get_type())
#define GST_GVA_STREAMMUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_STREAMMUX, GstGvaStreammux))
#define GST_GVA_STREAMMUX_CLASS(klass)                                                                                 \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_STREAMMUX, GstGvaStreammuxClass))
#define GST_IS_GVA_STREAMMUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_STREAMMUX))
#define GST_IS_GVA_STREAMMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_STREAMMUX))

typedef struct _GstGvaStreammux GstGvaStreammux;
typedef struct _GstGvaStreammuxClass GstGvaStreammuxClass;

/**
 * _GstGvaStreammux:
 *
 * A stream muxer element that collects video frames from multiple sink pads
 * and outputs them in round-robin order through a single source pad.
 * Each output buffer is tagged with GstGvaStreammuxMeta carrying source_id.
 *
 * Properties:
 *  - max-same-source-frames: max frames from one source per batch cycle (default 1)
 *  - max-fps: maximum output frame rate (0 = unlimited)
 *  - min-fps: minimum output frame rate (0 = no minimum)
 *  - sync-inputs: synchronize input streams by PTS
 *  - max-latency: maximum additional latency in nanoseconds
 *  - frame-duration: expected frame duration in nanoseconds (0 = auto)
 */
struct _GstGvaStreammux {
    GstElement element;

    GstPad *srcpad;

    /* Properties */
    guint max_same_source_frames;
    gdouble max_fps;
    gdouble min_fps;
    gboolean sync_inputs;
    GstClockTime max_latency;
    GstClockTime frame_duration;

    /* Internal state */
    guint num_sink_pads;
    guint64 batch_id;
    gboolean started;
    gboolean send_stream_start;
    gboolean eos_pending;

    /* Synchronization */
    GMutex lock;
    GCond cond;

    /* Sink pads list (ordered by pad index) */
    GList *sinkpads;

    /* Output caps negotiated */
    GstCaps *current_caps;

    /* Segment tracking */
    gboolean segment_sent;
    GstSegment segment;

    /* FPS control */
    GstClockTime last_output_time;
    GstClockTime min_fps_duration;
    GstClockTime max_fps_duration;

    /* Collect pads for synchronized buffer collection */
    GstCollectPads *collect;
};

struct _GstGvaStreammuxClass {
    GstElementClass parent_class;
};

GType gst_gva_streammux_get_type(void);

G_END_DECLS

#endif /* __GST_GVA_STREAMMUX_H__ */
