/*******************************************************************************
 * Copyright (C) 2022-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#ifdef __cplusplus

#include "inference_backend/buffer_mapper.h"

#include <memory>

// Channel (GvaBaseInference) specific information. Contains C++ objects
struct GvaBaseInferencePrivate {
    // Decoder VA display, if present
    dlstreamer::ContextPtr va_display;
    // Decoder D3D11Device, if present
    dlstreamer::ContextPtr d3d11_device;

    std::unique_ptr<InferenceBackend::BufferToImageMapper> buffer_mapper;
};

#endif // __cplusplus
