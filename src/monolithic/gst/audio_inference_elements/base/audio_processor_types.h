/*******************************************************************************
 * Copyright (C) 2019-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#ifdef __cplusplus

#include "gva_audio_event_meta.h"
#include "inference_backend/image_inference.h"
#include "utils.h"
#include <algorithm>
#include <functional>
#include <gst/gst.h>
#include <map>
#include <string>
#include <vector>

struct _GvaAudioBaseInference;
typedef struct _GvaAudioBaseInference GvaAudioBaseInference;
struct AudioInferenceFrame {
    GstBuffer *buffer;
    std::vector<float> samples;
    guint64 startTime;
    guint64 endTime;
};

struct AudioInferenceOutput {
    std::string model_name;
    std::map<std::string, std::map<uint32_t, std::pair<std::string, float>>> model_proc;
    std::map<std::string, InferenceBackend::OutputBlob::Ptr> output_tensors;
};
typedef int (*AudioNumOfSamplesRequired)(GvaAudioBaseInference *audio_base_inference);
typedef std::vector<float> (*AudioPreProcFunction)(AudioInferenceFrame *frame);
typedef void (*AudioPostProcFunction)(AudioInferenceFrame *frame, AudioInferenceOutput *output);

#else // __cplusplus

typedef void *AudioPreProcFunction;
typedef void *AudioPostProcFunction;
typedef void *AudioNumOfSamplesRequired;

#endif // __cplusplus
