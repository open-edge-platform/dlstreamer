/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GVA_AUDIO_EVENT_META_H__
#define __GVA_AUDIO_EVENT_META_H__

#include "gva_export.h"
#include <gst/gst.h>
#define GVA_AUDIO_EVENT_META_API_NAME "GstGVAAudioEventMetaAPI"
#define GVA_AUDIO_EVENT_META_IMPL_NAME "GstGVAAudioEventMeta"

G_BEGIN_DECLS

/**
 * @struct GstGVAAudioEventMeta
 * @brief Extra buffer metadata describing an audio frame event details
 * @var GstGVAAudioEventMeta::meta
 *   Parent #GstMeta
 * @var GstGVAAudioEventMeta::event_type
 *   GQuark describing the semantic of the AudioEvent (f.i. a Sound, a Speech, Silence)
 * @var GstGVAAudioEventMeta::id
 *   Identifier of this particular event
 * @var GstGVAAudioEventMeta::start_timestamp
 *   Start time stamp of the segment
 * @var GstGVAAudioEventMeta::end_timestamp
 *   End time stamp of the segment
 * @var GstGVAAudioEventMeta::params
 *  List of #GstStructure containing element-specific params for downstream,
 *  see @ref gst_gva_audio_event_meta_add_param().
 */
typedef struct {
    GstMeta meta;
    GQuark event_type;
    gint id;
    guint64 start_timestamp;
    guint64 end_timestamp;

    GList *params;
} GstGVAAudioEventMeta;

DLS_EXPORT GType gst_gva_audio_event_meta_api_get_type(void);
#define GST_GVA_AUDIO_EVENT_META_API_TYPE (gst_gva_audio_event_meta_api_get_type())

const GstMetaInfo *gst_gva_audio_event_meta_get_info(void);
#define GST_GVA_AUDIO_EVENT_META_INFO (gst_gva_audio_event_meta_get_info())

#define gst_gva_buffer_get_audio_event_meta(b)                                                                         \
    ((GvaAudioEventMeta *)gst_buffer_get_meta((b), GST_GVA_AUDIO_EVENT_META_API_TYPE))

GstGVAAudioEventMeta *gst_gva_buffer_get_audio_event_meta_id(GstBuffer *buffer, gint id);

DLS_EXPORT GstGVAAudioEventMeta *gst_gva_buffer_add_audio_event_meta(GstBuffer *buffer, const gchar *event_type,
                                                                     gulong start_timestamp, gulong end_timestamp);

GstGVAAudioEventMeta *gst_gva_buffer_add_audio_event_meta_id(GstBuffer *buffer, GQuark event_type,
                                                             gulong start_timestamp, gulong end_timestamp);
DLS_EXPORT void gst_gva_audio_event_meta_add_param(GstGVAAudioEventMeta *meta, GstStructure *s);

GstStructure *gst_gva_audio_event_meta_get_param(GstGVAAudioEventMeta *meta, const gchar *name);

G_END_DECLS

#endif /* __GVA_AUDIO_EVENT_META_H__*/
