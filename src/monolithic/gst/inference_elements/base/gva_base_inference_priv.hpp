/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
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
    // Decoder D3D12Device, if present
    dlstreamer::ContextPtr d3d12_device;

    std::unique_ptr<InferenceBackend::BufferToImageMapper> buffer_mapper;
};

#endif // __cplusplus
