/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_GVA_STREAMMUX_META_H__
#define __GST_GVA_STREAMMUX_META_H__

#include "gva_export.h"
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_GVA_STREAMMUX_META_API_TYPE (gst_gva_streammux_meta_api_get_type())
#define GST_GVA_STREAMMUX_META_INFO (gst_gva_streammux_meta_get_info())

typedef struct _GstGvaStreammuxMeta GstGvaStreammuxMeta;

/**
 * GstGvaStreammuxMeta:
 * @meta: parent #GstMeta
 * @source_id: the source/pad index this buffer originated from
 * @batch_id: the batch cycle number
 * @num_sources: total number of sources in the mux at the time of batching
 *
 * Metadata attached by gvastreammux to identify source origin of each buffer.
 */
struct _GstGvaStreammuxMeta {
    GstMeta meta;
    guint source_id;
    guint64 batch_id;
    guint num_sources;
};

DLS_EXPORT GType gst_gva_streammux_meta_api_get_type(void);
DLS_EXPORT const GstMetaInfo *gst_gva_streammux_meta_get_info(void);

DLS_EXPORT GstGvaStreammuxMeta *gst_buffer_add_gva_streammux_meta(GstBuffer *buffer, guint source_id, guint64 batch_id,
                                                                  guint num_sources);
DLS_EXPORT GstGvaStreammuxMeta *gst_buffer_get_gva_streammux_meta(GstBuffer *buffer);

G_END_DECLS

#endif /* __GST_GVA_STREAMMUX_META_H__ */
