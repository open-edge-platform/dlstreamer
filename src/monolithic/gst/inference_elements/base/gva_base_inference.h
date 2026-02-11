/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "config.h"

#include "gva_caps.h"
#include "inference_singleton.h"
#include "processor_types.h"

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_BASE_INFERENCE_REGION (gst_gva_base_inference_get_inf_region())
#define GST_TYPE_GVA_BASE_INFERENCE (gva_base_inference_get_type())
#define GVA_BASE_INFERENCE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_BASE_INFERENCE, GvaBaseInference))
#define GVA_BASE_INFERENCE_CLASS(klass)                                                                                \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_BASE_INFERENCE, GvaBaseInferenceClass))
#define GVA_BASE_INFERENCE_GET_CLASS(obj)                                                                              \
    (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_GVA_BASE_INFERENCE, GvaBaseInferenceClass))

typedef void (*OnBaseInferenceInitializedFunction)(GvaBaseInference *base_inference);

typedef enum { GST_GVA_DETECT_TYPE, GST_GVA_CLASSIFY_TYPE, GST_GVA_INFERENCE_TYPE } InferenceType;
typedef enum { FULL_FRAME, ROI_LIST } InferenceRegionType;

typedef struct _GvaBaseInference {
    GstBaseTransform base_transform;

    // properties
    gboolean no_block;
    gboolean reshape;
    gboolean share_va_display_ctx;
    guint inference_interval;
    guint batch_size;
    gint batch_timeout;
    guint reshape_width;
    guint reshape_height;
    guint nireq;
    guint cpu_streams;
    guint gpu_streams;
    gchar *model;
    gchar *model_proc;
    gchar *device;
    gchar *model_instance_id;
    gchar *scheduling_policy;
    gchar *ie_config;
    gchar *pre_proc_config;
    gchar *allocator_name;
    gchar *pre_proc_type;
    gchar *object_class;
    gchar *labels;
    gchar *scale_method;
    gchar *custom_preproc_lib;
    gchar *custom_postproc_lib;
    gchar *ov_extension_lib;

    // other fields
    struct GvaBaseInferencePrivate *priv;

    InferenceType type;
    GstVideoInfo *info;
    CapsFeature caps_feature;
    InferenceRegionType inference_region;

    InferenceImpl *inference;

    FilterROIFunction is_roi_inference_needed;
    FilterROIFunction specific_roi_filter;

    PreProcFunction pre_proc;
    InputPreprocessorsFactory input_prerocessors_factory;
    PostProcessor *post_proc;

    gboolean initialized;
    guint64 num_skipped_frames;
    guint64 frame_num;

    GMutex meta_mutex;
} GvaBaseInference;

typedef struct _GvaBaseInferenceClass {
    GstBaseTransformClass base_transform_class;

    OnBaseInferenceInitializedFunction on_initialized;
} GvaBaseInferenceClass;

GType gva_base_inference_get_type(void);
GType gst_gva_base_inference_get_inf_region(void);
gboolean check_gva_base_inference_stopped(GvaBaseInference *base_inference);

G_END_DECLS
