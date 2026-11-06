/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "post_processor_c.h"
#include "post_processor_impl.h"

#include "utils.h"
#include <inference_backend/logger.h>

post_processing::PostProcessor *createPostProcessor(InferenceImpl *inference_impl, GvaBaseInference *base_inference) {
    if (inference_impl == nullptr) {
        GVA_WARNING("InferenceImpl is null. Creating of inference post processor is imposible");
        return nullptr;
    }
    PostProcessor *post_processor = nullptr;
    try {
        post_processor = new post_processing::PostProcessor(inference_impl, base_inference);
    } catch (const std::exception &e) {
        GVA_ERROR("Couldn't create post-processor: %s", Utils::createNestedErrorMsg(e).c_str());
    }
    return post_processor;
}

void releasePostProcessor(post_processing::PostProcessor *post_processor) {
    if (post_processor)
        delete post_processor;
}
