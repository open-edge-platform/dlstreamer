/*******************************************************************************
 * Copyright (C) 2018-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "config.h"
#include <gva_audio_base_inference.h>

G_BEGIN_DECLS

#define GST_TYPE_GVA_AUDIO_DETECT (gst_gva_audio_detect_get_type())
#define GVA_AUDIO_DETECT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_AUDIO_DETECT, GvaAudioDetect))

typedef struct _GvaAudioDetect {
    GvaAudioBaseInference audio_base_inference;
    guint req_num_samples;
} GvaAudioDetect;

typedef struct _GvaAudioDetectClass {
    GvaAudioBaseInferenceClass audio_base_inference_class;
} GvaAudioDetectClass;

GType gst_gva_audio_detect_get_type(void);

G_END_DECLS
