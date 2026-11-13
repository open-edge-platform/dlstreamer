/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include <processor_types.h>

#ifdef __cplusplus
class InferenceImpl;
#else  /* __cplusplus */
typedef struct InferenceImpl InferenceImpl;
#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct _GvaBaseInference;
typedef struct _GvaBaseInference GvaBaseInference;

gboolean registerElement(GvaBaseInference *base_inference);
void release_inference_instance(GvaBaseInference *base_inference);

#ifdef __cplusplus
} /* extern C */
#endif /* __cplusplus */
