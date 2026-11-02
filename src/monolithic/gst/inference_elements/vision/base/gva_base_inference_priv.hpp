/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#ifdef __cplusplus

#include "inference_backend/buffer_mapper.h"

#include "common/post_processor.h"

#include <memory>

// Channel (GvaBaseInference) specific information. Contains C++ objects
struct GvaBaseInferencePrivate {
    // Decoder VA display, if present
    dlstreamer::ContextPtr va_display;
    // Decoder D3D11Device, if present
    dlstreamer::ContextPtr d3d11_device;

    std::unique_ptr<InferenceBackend::BufferToImageMapper> buffer_mapper;

    // Owner of the post-processor. GvaBaseInference::post_proc points to it. It is replaced when caps are renegotiated,
    // frames that are still being processed keep the one they started with until they are done.
    // Guarded by the object lock of the element.
    std::shared_ptr<post_processing::PostProcessor> post_proc;
};

#endif // __cplusplus
