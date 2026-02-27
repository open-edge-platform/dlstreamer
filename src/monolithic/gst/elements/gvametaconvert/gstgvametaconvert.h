/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef _GST_GVA_META_CONVERT_H_
#define _GST_GVA_META_CONVERT_H_

#ifdef AUDIO
#include <gst/audio/audio.h>
#endif
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN(gst_gva_meta_convert_debug_category);

#define GST_TYPE_GVA_METACONVERT_FORMAT (gst_gva_metaconvert_get_format())
#define GST_TYPE_GVA_META_CONVERT (gst_gva_meta_convert_get_type())
#define GST_GVA_META_CONVERT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_META_CONVERT, GstGvaMetaConvert))
#define GST_GVA_META_CONVERT_CLASS(klass)                                                                              \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_META_CONVERT, GstGvaMetaConvertClass))
#define GST_IS_GVA_META_CONVERT(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_META_CONVERT))
#define GST_IS_GVA_META_CONVERT_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_META_CONVERT))
typedef struct _GstGvaMetaConvert GstGvaMetaConvert;
typedef struct _GstGvaMetaConvertClass GstGvaMetaConvertClass;

typedef gboolean (*convert_function_type)(GstGvaMetaConvert *converter, GstBuffer *buffer);

typedef enum {
    GST_GVA_METACONVERT_JSON,
    GST_GVA_METACONVERT_DUMP_DETECTION,
} GstGVAMetaconvertFormatType;

struct _GstGvaMetaConvert {
    GstBaseTransform base_gvametaconvert;

    GstGVAMetaconvertFormatType format;
    gboolean add_tensor_data;
    gchar *source;
    gchar *tags;
    gboolean add_empty_detection_results;
    gboolean signal_handoffs;
    gboolean timestamp_utc;
    gboolean timestamp_microseconds;
    gboolean timestamp_rtp;
    convert_function_type convert_function;
    GstVideoInfo *info;
#ifdef AUDIO
    GstAudioInfo *audio_info;
#endif
    gint json_indent;
};

struct _GstGvaMetaConvertClass {
    GstBaseTransformClass base_gvametaconvert_class;

    void (*handoff)(GstElement *element, GstBuffer *buf);
};

// FIXME: missed implementation
GType gst_gva_meta_convert_get_type(void);
GType gst_gva_metaconvert_get_format(void);

G_END_DECLS

#endif
