/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <processor_types.h>

#ifdef __cplusplus
class InferenceCoordinator;
#else  /* __cplusplus */
typedef struct InferenceCoordinator InferenceCoordinator;
#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif

PostProcessor *createPostProcessor(InferenceCoordinator *coordinator, GvaBaseInference *base_inference);
void releasePostProcessor(PostProcessor *post_processor);

#ifdef __cplusplus
}
#endif
