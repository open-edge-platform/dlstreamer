/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_GVA_STREAMMUX_H__
#define __GST_GVA_STREAMMUX_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_STREAMMUX (gst_gva_streammux_get_type())
#define GST_GVA_STREAMMUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_STREAMMUX, GstGvaStreammux))
#define GST_GVA_STREAMMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_STREAMMUX, GstGvaStreammuxClass))
#define GST_IS_GVA_STREAMMUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_STREAMMUX))
#define GST_IS_GVA_STREAMMUX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_STREAMMUX))

#define GST_GVA_STREAMMUX_MAX_PAD_INDEX 256

typedef enum {
    GVA_STREAMMUX_SYNC_MODE_NONE = 0,
    GVA_STREAMMUX_SYNC_MODE_FIRST_PTS,
    GVA_STREAMMUX_SYNC_MODE_SEGMENT,
    GVA_STREAMMUX_SYNC_MODE_PIPELINE,
    GVA_STREAMMUX_SYNC_MODE_NTP,
} GvaStreammuxSyncMode;

#define GST_TYPE_GVA_STREAMMUX_SYNC_MODE (gst_gva_streammux_sync_mode_get_type())
GType gst_gva_streammux_sync_mode_get_type(void);

typedef struct _GstGvaStreammux GstGvaStreammux;
typedef struct _GstGvaStreammuxClass GstGvaStreammuxClass;
typedef struct _GvaStreammuxPadData GvaStreammuxPadData;

struct _GvaStreammuxPadData {
    GstPad *pad;
    guint pad_index;
    GQueue buffer_queue;
    gboolean eos;
    gboolean flushing;

    /* PTS normalization state (used by sync-mode) */
    gboolean first_pts_set;
    GstClockTime first_pts;
    GstClockTime segment_start;
};

struct _GstGvaStreammux {
    GstElement element;

    GstPad *srcpad;

    /* Properties */
    gdouble max_fps;
    GstClockTime pts_tolerance;
    GstClockTime max_wait_time;
    guint max_queue_size;
    GvaStreammuxSyncMode sync_mode;

    /* Internal state */
    guint num_sink_pads;
    gboolean started;
    gboolean send_stream_start;
    gboolean flushing;

    /* Number of sink pads currently between FLUSH_START and FLUSH_STOP.
     * Used to coalesce per-pad flush events into a single downstream flush. */
    guint flushing_pads_count;

    /* Synchronization */
    GMutex lock;
    GCond cond;

    /* Per-pad data array (GPtrArray of GvaStreammuxPadData*) */
    GPtrArray *pad_data;

    /* Sink pads list (ordered by pad index) */
    GList *sinkpads;

    /* Output caps negotiated */
    GstCaps *current_caps;

    /* Segment tracking */
    gboolean segment_sent;
    GstSegment segment;

    /* FPS control */
    GstClockTime last_output_time;
    GstClockTime max_fps_duration;

    /* Batch PTS tracking */
    GstClockTime batch_anchor_pts;
    gint64 batch_start_real_time;
    GstClockTime last_pushed_batch_pts;

    /* Output task */
    guint eos_pad_count;
};

struct _GstGvaStreammuxClass {
    GstElementClass parent_class;
};

GType gst_gva_streammux_get_type(void);

G_END_DECLS

#endif /* __GST_GVA_STREAMMUX_H__ */
