/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "audio_processor_types.h"
#include "processor.h"
#include "utils.h"
#include <gst/audio/audio.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_AUDIO_BASE_INFERENCE (gva_audio_base_inference_get_type())
#define GVA_AUDIO_BASE_INFERENCE(obj)                                                                                  \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_AUDIO_BASE_INFERENCE, GvaAudioBaseInference))

typedef struct _GvaAudioBaseInference {
    GstBaseTransform audio_base_transform;
    // properties
    gdouble sliding_length;
    gdouble threshold;
    gchar *model;
    gchar *model_proc;
    gchar *device;

    // other fields
    gboolean values_checked;
    guint sample_length;
    // smart pointers cannot be used because of mixed c and c++ code
    OpenVINOAudioInference *inf_handle;
    AudioInferImpl *impl_handle;
    AudioPreProcFunction pre_proc;
    AudioPostProcFunction post_proc;
    AudioNumOfSamplesRequired req_sample_size;
} GvaAudioBaseInference;

typedef struct _GvaAudioBaseInferenceClass {
    GstBaseTransformClass base_transform_class;
} GvaAudioBaseInferenceClass;

GType gva_audio_base_inference_get_type(void);

G_END_DECLS
