/*******************************************************************************
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __AUDIO_PROCESSOR__
#define __AUDIO_PROCESSOR__

#include <gst/base/gstbasetransform.h>

#ifdef __cplusplus
class AudioInferImpl;
class OpenVINOAudioInference;
#else  /* __cplusplus */
typedef struct AudioInferImpl AudioInferImpl;
typedef struct OpenVINOAudioInference OpenVINOAudioInference;
#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif
struct _GvaAudioBaseInference;
typedef struct _GvaAudioBaseInference GvaAudioBaseInference;
GstFlowReturn infer_audio(GvaAudioBaseInference *audio_base_inference, GstBuffer *buf, GstClockTime start_time);
gboolean create_handles(GvaAudioBaseInference *audio_base_inference);
void delete_handles(GvaAudioBaseInference *audio_base_inference);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_PROCESSOR__ */
