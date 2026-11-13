/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "config.h"
#include <gst/base/gstbasetransform.h>
#include <memory>
#include <mutex>
#include <vector>

#include "gstgvaaudiotranscribehandler.h" // Handler interface

G_BEGIN_DECLS

#define GST_TYPE_GVA_AUDIO_TRANSCRIBE (gst_gva_audio_transcribe_get_type())
#define GVA_AUDIO_TRANSCRIBE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_AUDIO_TRANSCRIBE, GvaAudioTranscribe))
#define GVA_AUDIO_TRANSCRIBE_CLASS(klass)                                                                              \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_AUDIO_TRANSCRIBE, GvaAudioTranscribeClass))
#define GST_IS_GVA_AUDIO_TRANSCRIBE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_AUDIO_TRANSCRIBE))
#define GST_IS_GVA_AUDIO_TRANSCRIBE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_AUDIO_TRANSCRIBE))

typedef struct _GvaAudioTranscribe GvaAudioTranscribe;
typedef struct _GvaAudioTranscribeClass GvaAudioTranscribeClass;

struct _GvaAudioTranscribe {
    GstBaseTransform base;

    /* properties */
    gchar *model_path;          /* path to the model (Whisper directory, or custom model path) */
    gchar *device;              /* inference device (CPU, GPU, etc.) */
    gchar *model_type;          /* model type: whisper (default), custom types can be implemented */
    gchar *language;            /* language code for transcription */
    gchar *task;                /* task: transcribe or translate */
    gboolean return_timestamps; /* whether to return timestamps */

    /* modular handler */
    GvaAudioTranscribeHandler *handler; /* handler implementation - extensible for custom models */

    /* shared audio accumulation state */
    std::vector<float> *audio_data; /* buffer for audio samples */
    std::mutex *mutex;              /* mutex for thread-safe audio buffer access */
};

struct _GvaAudioTranscribeClass {
    GstBaseTransformClass base_class;
};

GType gst_gva_audio_transcribe_get_type(void);

G_END_DECLS