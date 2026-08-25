/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include <processor_types.h>

#ifdef __cplusplus
class InferenceCoordinator;
#else  /* __cplusplus */
typedef struct InferenceCoordinator InferenceCoordinator;
#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

struct _GvaBaseInference;
typedef struct _GvaBaseInference GvaBaseInference;

gboolean register_element(GvaBaseInference *base_inference);
void release_inference_coordinator(GvaBaseInference *base_inference);

#ifdef __cplusplus
} /* extern C */
#endif /* __cplusplus */
