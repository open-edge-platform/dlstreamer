/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "dlstreamer/gst/metadata/gva_audio_event_meta.h"
#include <string.h>
#define UNUSED(x) (void)(x)

DLS_EXPORT GType gst_gva_audio_event_meta_api_get_type(void) {
    static GType type;
    static const gchar *tags[] = {NULL};

    if (g_once_init_enter(&type)) {
        GType _type = gst_meta_api_type_register(GVA_AUDIO_EVENT_META_API_NAME, tags);
        g_once_init_leave(&type, _type);
    }
    return type;
}

gboolean gst_gva_audio_event_meta_init(GstMeta *meta, gpointer params, GstBuffer *buffer) {
    UNUSED(params);
    UNUSED(buffer);
    GstGVAAudioEventMeta *event_meta = (GstGVAAudioEventMeta *)meta;
    event_meta->event_type = 0;
    event_meta->id = 0;
    event_meta->start_timestamp = event_meta->end_timestamp = 0;
    event_meta->params = NULL;

    return TRUE;
}

void gst_gva_audio_event_meta_free(GstMeta *meta, GstBuffer *buffer) {
    UNUSED(buffer);
    GstGVAAudioEventMeta *event_meta = (GstGVAAudioEventMeta *)meta;

    g_list_free_full(event_meta->params, (GDestroyNotify)gst_structure_free);
}

gboolean gst_gva_audio_event_meta_transform(GstBuffer *dest_buf, GstMeta *src_meta, GstBuffer *src_buf, GQuark type,
                                            gpointer data) {
    UNUSED(src_buf);
    UNUSED(type);
    UNUSED(data);
    GstGVAAudioEventMeta *src = (GstGVAAudioEventMeta *)src_meta;

    GST_DEBUG("copy Audio Event metadata");
    GstGVAAudioEventMeta *dst =
        gst_gva_buffer_add_audio_event_meta_id(dest_buf, src->event_type, src->start_timestamp, src->end_timestamp);
    if (!dst)
        return FALSE;

    dst->id = src->id;
    dst->params = g_list_copy_deep(src->params, (GCopyFunc)(GCallback)gst_structure_copy, NULL);
    return TRUE;
}

const GstMetaInfo *gst_gva_audio_event_meta_get_info(void) {
    static const GstMetaInfo *meta_info = NULL;

    if (g_once_init_enter(&meta_info)) {
        const GstMetaInfo *meta = gst_meta_register(
            GST_GVA_AUDIO_EVENT_META_API_TYPE, GVA_AUDIO_EVENT_META_IMPL_NAME, sizeof(GstGVAAudioEventMeta),
            (GstMetaInitFunction)gst_gva_audio_event_meta_init, (GstMetaFreeFunction)gst_gva_audio_event_meta_free,
            (GstMetaTransformFunction)gst_gva_audio_event_meta_transform);
        g_once_init_leave(&meta_info, meta);
    }
    return meta_info;
}

/**
 * gst_gva_buffer_get_audio_event_meta_id:
 * @buffer: a #GstBuffer
 * @id: a metadata id
 *
 * Find the #GstGVAAudioEventMeta on @buffer with the given @id.
 *
 * Buffers can contain multiple #GstGVAAudioEventMeta metadata items if
 * multiple events detected in frame.
 *
 * Returns: (transfer none): the #GstGVAAudioEventMeta with @id or %NULL when there is
 * no such metadata on @buffer.
 */
GstGVAAudioEventMeta *gst_gva_buffer_get_audio_event_meta_id(GstBuffer *buffer, gint id) {
    gpointer state = NULL;
    GstMeta *meta;
    const GstMetaInfo *meta_info = GST_GVA_AUDIO_EVENT_META_INFO;

    while ((meta = gst_buffer_iterate_meta(buffer, &state))) {
        if (meta->info->api == meta_info->api) {
            GstGVAAudioEventMeta *event_meta = (GstGVAAudioEventMeta *)meta;
            if (event_meta->id == id)
                return event_meta;
        }
    }
    return NULL;
}

/**
 * gst_gva_buffer_add_audio_event_meta:
 * @buffer: a #GstBuffer
 * @event_type: GQuark describing the semantic of the AudioEvent (f.i. a Sound, a Speech, Silence)
 * @start_timestamp: start time stamp of the segment
 * @end_timestamp: end time stamp of the segment
 *
 * Attaches #GstGVAAudioEventMeta metadata to @buffer with the given
 * parameters.
 *
 * Returns: (transfer none): the #GstGVAAudioEventMeta on @buffer.
 */
DLS_EXPORT GstGVAAudioEventMeta *gst_gva_buffer_add_audio_event_meta(GstBuffer *buffer, const gchar *event_type,
                                                                     guint64 start_timestamp, guint64 end_timestamp) {
    return gst_gva_buffer_add_audio_event_meta_id(buffer, g_quark_from_string(event_type), start_timestamp,
                                                  end_timestamp);
}

/**
 * gst_gva_buffer_add_audio_event_meta_id:
 * @buffer: a #GstBuffer
 * @event_type: GQuark describing the semantic of the AudioEvent (f.i. a Sound, a Speech, Silence)
 * @start_timestamp: start time stamp of the segment
 * @end_timestamp: end time stamp of the segment
 *
 * Attaches #GstGVAAudioEventMeta metadata to @buffer with the given
 * parameters.
 *
 * Returns: (transfer none): the #GstGVAAudioEventMeta on @buffer.
 */
GstGVAAudioEventMeta *gst_gva_buffer_add_audio_event_meta_id(GstBuffer *buffer, GQuark event_type,
                                                             guint64 start_timestamp, guint64 end_timestamp) {
    GstGVAAudioEventMeta *event_meta;

    g_return_val_if_fail(GST_IS_BUFFER(buffer), NULL);
    g_return_val_if_fail(gst_buffer_is_writable(buffer), NULL);

    event_meta = (GstGVAAudioEventMeta *)gst_buffer_add_meta(buffer, GST_GVA_AUDIO_EVENT_META_INFO, NULL);
    event_meta->event_type = event_type;
    event_meta->start_timestamp = start_timestamp;
    event_meta->end_timestamp = end_timestamp;

    return event_meta;
}

/**
 * gst_gva_audio_event_meta_add_param:
 * @meta: a #GstGVAAudioEventMeta
 * @structure: (transfer full): a #GstStructure
 *
 * Attach element-specific parameters to @meta meant to be used by downstream
 * elements which may handle this event.
 * The name of @structure is used to identify the element these parameters are meant for.
 */
DLS_EXPORT void gst_gva_audio_event_meta_add_param(GstGVAAudioEventMeta *meta, GstStructure *structure) {
    g_return_if_fail(meta);
    g_return_if_fail(structure);

    meta->params = g_list_append(meta->params, structure);
}

/**
 * gst_gva_audio_event_meta_get_param:
 * @meta: a #GstGVAAudioEventMeta
 *
 * Retrieve the parameter for @meta having @name as structure name,
 * or %NULL if there is none.
 *
 * Returns: (transfer none) (nullable): a #GstStructure
 *
 * See also: gst_gva_audio_event_meta_add_param()
 */
GstStructure *gst_gva_audio_event_meta_get_param(GstGVAAudioEventMeta *meta, const gchar *name) {
    GList *list;

    g_return_val_if_fail(meta, NULL);
    g_return_val_if_fail(name, NULL);

    for (list = meta->params; list; list = g_list_next(list)) {
        GstStructure *structure = list->data;
        if (gst_structure_has_name(structure, name))
            return structure;
    }
    return NULL;
}
