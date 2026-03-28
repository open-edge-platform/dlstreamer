/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_GVA_STREAMDEMUX_H__
#define __GST_GVA_STREAMDEMUX_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_STREAMDEMUX (gst_gva_streamdemux_get_type())
#define GST_GVA_STREAMDEMUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_STREAMDEMUX, GstGvaStreamdemux))
#define GST_GVA_STREAMDEMUX_CLASS(klass)                                                                               \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_STREAMDEMUX, GstGvaStreamdemuxClass))
#define GST_IS_GVA_STREAMDEMUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_STREAMDEMUX))
#define GST_IS_GVA_STREAMDEMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_STREAMDEMUX))

typedef struct _GstGvaStreamdemux GstGvaStreamdemux;
typedef struct _GstGvaStreamdemuxClass GstGvaStreamdemuxClass;

/**
 * _GstGvaStreamdemux:
 *
 * A stream demuxer element that routes buffers from a single sink pad
 * to multiple source pads based on GstGvaStreammuxMeta source_id.
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

    /* Synchronization */
    GMutex lock;

    /* Src pads array (indexed by source_id) */
    GPtrArray *srcpads;

    /* Per-src-pad last batch_id for ordering check */
    GArray *last_batch_ids;

    /* FPS control */
    GstClockTime last_output_time;
    GstClockTime max_fps_duration;
};

struct _GstGvaStreamdemuxClass {
    GstElementClass parent_class;
};

GType gst_gva_streamdemux_get_type(void);

G_END_DECLS

#endif /* __GST_GVA_STREAMDEMUX_H__ */
