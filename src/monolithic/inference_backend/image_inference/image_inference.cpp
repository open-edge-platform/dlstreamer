/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "openvino_image_inference.h"
#include "utils.h"
#ifdef _WIN32
#include "image_inference_async_d3d11.h"
#else
#include "image_inference_async/image_inference_async.h"
#include <cstdlib>
#include <unistd.h>
#endif

using namespace InferenceBackend;

namespace {

ImagePreprocessorType getPreProcType(const std::map<std::string, std::string> &base_config) {
    auto it = base_config.find(KEY_PRE_PROCESSOR_TYPE);
    if (it == base_config.end())
        throw std::runtime_error("Image pre-processor type is not set");
    return static_cast<ImagePreprocessorType>(std::stoi(it->second));
}

} // namespace

std::map<std::string, GstStructure *> ImageInference::GetModelInfoPreproc(const std::string model_file,
                                                                          const gchar *preproc_config,
                                                                          const gchar *ov_extension_lib) {
    return OpenVINOImageInference::GetModelInfoPreproc(model_file, preproc_config, ov_extension_lib);
}

ImageInference::Ptr ImageInference::createImageInferenceInstance(MemoryType input_image_memory_type,
                                                                 const InferenceConfig &config, Allocator *allocator,
                                                                 CallbackFunc callback, ErrorHandlingFunc error_handler,
                                                                 dlstreamer::ContextPtr context) {
    // Flag to determine if asynchronous mode is required
    bool async_mode = false;

    // Determine the memory type to be used for inference
    MemoryType memory_type_to_use = MemoryType::ANY;

#ifndef _WIN32
    // Determine if the device is an NPU
    bool isNpu = (config.at(KEY_BASE).at(KEY_DEVICE).find("NPU") != std::string::npos);
#endif

    switch (input_image_memory_type) {
    case MemoryType::SYSTEM:
        // Use system memory directly
        memory_type_to_use = input_image_memory_type;
        break;

    case MemoryType::DMA_BUFFER:
    case MemoryType::VAAPI: {
        // Enable asynchronous mode for DMA_BUFFER and VAAPI
        async_mode = true;

        // Ensure context is provided for VAAPI
        if (!context) {
            throw std::invalid_argument("Null context provided (VaApiContext is expected)");
        }

        // Determine the preprocessor type based on configuration
        ImagePreprocessorType preproc_type = getPreProcType(config.at(KEY_BASE));
        switch (preproc_type) {
        case ImagePreprocessorType::VAAPI_SYSTEM:
            // Use system memory for VAAPI_SYSTEM preprocessor type
            memory_type_to_use = MemoryType::SYSTEM;
#ifndef _WIN32
            // DMA-BUF zero-copy (VPP writes into DMA-BUF, NPU reads from same buffer) is used on NPU
            // when the DMA-BUF heap is accessible; otherwise fall back to the plain SYSTEM-memory path.
            // The heap access is probed here (not later, per-surface) so the whole pipeline stays
            // consistent: a partial fallback would leave the pool/OpenVINO instance in DMA_BUFFER mode
            // while individual surfaces are SYSTEM, which deadlocks inference.
            // Zero-copy can be disabled by setting GVA_NPU_ZERO_COPY=0 (enabled by default).
            {
                const char *zero_copy_env = std::getenv("GVA_NPU_ZERO_COPY");
                bool zero_copy_enabled = !(zero_copy_env && std::string(zero_copy_env) == "0");
                if (isNpu && zero_copy_enabled) {
                    if (access("/dev/dma_heap/system", R_OK | W_OK) != 0) {
                        GVA_WARNING("Falling back to the slow NPU path (extra GPU->CPU->NPU copies): no access to "
                                    "DMA-BUF. To enable zero-copy, grant access to /dev/dma_heap/system, e.g. "
                                    "'sudo chgrp video /dev/dma_heap/system && sudo chmod 660 /dev/dma_heap/system'.");
                    } else {
                        memory_type_to_use = MemoryType::DMA_BUFFER;
                    }
                }
            }
#endif
            break;
        case ImagePreprocessorType::VAAPI_SURFACE_SHARING:
            // Use VAAPI memory for VAAPI_SURFACE_SHARING preprocessor type
            memory_type_to_use = MemoryType::VAAPI;
            break;
        default:
            throw std::runtime_error("Incorrect pre-process-backend, should be vaapi or vaapi-surface-sharing");
        }
        break;
    }

    case MemoryType::D3D11: {
        async_mode = true;
        // Ensure context is provided for D3D11
        if (!context) {
            throw std::invalid_argument("Null context provided (D3D11Context is expected)");
        }
        // Determine the preprocessor type based on configuration
        ImagePreprocessorType preproc_type = getPreProcType(config.at(KEY_BASE));
        switch (preproc_type) {
        case ImagePreprocessorType::D3D11:
            memory_type_to_use = MemoryType::SYSTEM;
            break;
        case ImagePreprocessorType::D3D11_SURFACE_SHARING:
            memory_type_to_use = MemoryType::D3D11;
            throw std::runtime_error("Not implemented yet");
            break;
        default:
            throw std::runtime_error("Incorrect pre-process-backend, should be d3d11 or d3d11-surface-sharing");
        }
        break;
    }
    default:
        throw std::invalid_argument("Unsupported memory type");
    }

    // Create an OpenVINOImageInference instance with the determined memory type
    auto ov_inference = std::make_shared<OpenVINOImageInference>(config, allocator, context, callback, error_handler,
                                                                 memory_type_to_use);

    ImageInference::Ptr result_inference;
    if (async_mode) {
#ifndef _WIN32
        // Wrap the inference in an asynchronous handler if async mode is enabled
        result_inference = std::make_shared<ImageInferenceAsync>(config, context, std::move(ov_inference));
#else
        result_inference = std::make_shared<ImageInferenceAsyncD3D11>(config, context, std::move(ov_inference));
#endif
    } else {
        // Use the OpenVINO inference directly if not in async mode
        result_inference = std::move(ov_inference);
    }

    return result_inference;
}