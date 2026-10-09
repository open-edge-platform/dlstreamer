/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_GVA_STREAMDEMUX_H__
#define __GST_GVA_STREAMDEMUX_H__

#include <gst/base/gstflowcombiner.h>
#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_STREAMDEMUX (gst_gva_streamdemux_get_type())
#define GST_GVA_STREAMDEMUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_STREAMDEMUX, GstGvaStreamdemux))
#define GST_GVA_STREAMDEMUX_CLASS(klass)                                                                               \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_STREAMDEMUX, GstGvaStreamdemuxClass))
#define GST_IS_GVA_STREAMDEMUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_STREAMDEMUX))
#define GST_IS_GVA_STREAMDEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_STREAMDEMUX))

#define GST_GVA_STREAMDEMUX_MAX_PAD_INDEX 256

typedef struct _GstGvaStreamdemux GstGvaStreamdemux;
typedef struct _GstGvaStreamdemuxClass GstGvaStreamdemuxClass;

/**
 * _GstGvaStreamdemux:
 *
 * A stream demuxer element that routes buffers from a single sink pad
 * to multiple source pads based on GstAnalyticsBatchMeta streams[0].index.
 * Must be used with gvastreammux which attaches the required metadata.
 *
 * Properties:
 *  - max-fps: maximum output frame rate (0 = unlimited, for local file sources only)
 */
struct _GstGvaStreamdemux {
    GstElement element;

    GstPad *sinkpad;

    /* Properties */
    gdouble max_fps;

    /* Internal state */
    guint num_src_pads;
    gboolean validated;
    /* TRUE once the sink caps are the multistream batch container
     * (gvastreammux CONTAINER mode); FALSE for plain video passthrough. */
    gboolean container_mode;

    /* Synchronization */
    GMutex lock;

    /* Src pads array (indexed by source_id) */
    GPtrArray *srcpads;

    /* Combines the per-src-pad flow returns into the one value the chain
     * function reports upstream, so a single unlinked or finished branch does
     * not stop the other ones. Not MT-safe; only touch it with lock held. */
    GstFlowCombiner *flow_combiner;

    /* FPS control.
     *
     * PASSTHROUGH carries one source per buffer, so each source is throttled
     * against its own last output time, indexed by source id -- a single
     * shared time would divide max-fps between the sources instead of
     * applying it to each. CONTAINER carries every source in one buffer, so
     * one time covers the whole batch and therefore every source in it. */
    GArray *last_output_times;
    GstClockTime last_batch_output_time;
    GstClockTime max_fps_duration;
};

struct _GstGvaStreamdemuxClass {
    GstElementClass parent_class;
};

GType gst_gva_streamdemux_get_type(void);

G_END_DECLS

#endif /* __GST_GVA_STREAMDEMUX_H__ */
