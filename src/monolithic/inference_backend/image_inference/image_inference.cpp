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

#ifndef _WIN32
// Checks whether the NPU can consume the zero-copy DMA-BUF that VPP renders into. The VPP output is
// scaled to the model's image input size; for dynamic-shape models that size is resolved by the
// backend into KEY_RESHAPE_WIDTH/HEIGHT, so that is used first and the model's static input shape is
// only read as a fallback. The GPU requires a 64-byte aligned row pitch, so for other widths the
// DMA-BUF rows are padded and the NPU must support strided input tensors. Returns false (which keeps
// the pipeline on the plain SYSTEM-memory path) if the width cannot be determined or the padded
// layout cannot be consumed by the NPU, or if the model is batched (batch-size > 1).
bool isNpuZeroCopyInputSupported(const InferenceConfig &config) {
    try {
        const auto &base_config = config.at(KEY_BASE);

        // Resolve the effective model batch size the same way as the OpenVINO backend: 0 means auto
        // (device optimal batch size), and auto-batching (batch timeout set) keeps the model at batch 1.
        int batch_size = std::stoi(base_config.at(KEY_BATCH_SIZE));
        const auto inf_it = config.find(KEY_INFERENCE);
        const bool auto_batching = inf_it != config.end() && inf_it->second.count(ov::auto_batch_timeout.name()) &&
                                   std::stoi(inf_it->second.at(ov::auto_batch_timeout.name())) > -1;
        if (auto_batching)
            batch_size = 1;
        else if (batch_size == 0)
            batch_size = OpenVINOImageInference::GetDeviceOptimalBatchSize(base_config.at(KEY_DEVICE));
        if (batch_size > 1) {
            GVA_WARNING("NPU zero-copy: batch-size %d is not supported, only batch-size 1 is", batch_size);
            return false;
        }

        uint32_t width = 0;
        uint32_t height = 0;

        // Preferred source: the static model-input size the backend reshapes to (also the VPP output
        // size). This is populated for dynamic-shape models, which have no usable static input shape.
        auto rw_it = base_config.find(KEY_RESHAPE_WIDTH);
        auto rh_it = base_config.find(KEY_RESHAPE_HEIGHT);
        if (rw_it != base_config.end() && rh_it != base_config.end()) {
            width = static_cast<uint32_t>(std::stoul(rw_it->second));
            height = static_cast<uint32_t>(std::stoul(rh_it->second));
        }

        // Fallback for static models without an explicit reshape: read the model's input shape.
        if (width == 0 || height == 0) {
            const std::string model_file = base_config.at(KEY_MODEL);
            std::string ov_extension_lib;
            // Optional OpenVINO extension library (gvadetect/gvaclassify 'ov-extension-lib' property): a .so
            // defining custom OpenVINO operations, needed to read models that contain such operations.
            auto ext_it = base_config.find(KEY_OV_EXTENSION_LIB);
            if (ext_it != base_config.end())
                ov_extension_lib = ext_it->second;

            for (const auto &input :
                 OpenVINOImageInference::GetModelInputShapes(model_file, ov_extension_lib.c_str())) {
                const auto &shape = input.second;
                // Image input is the 4D tensor: the VA pre-processing path builds [1, C, H, W] tensors and
                // ov::set_batch needs an 'N' axis. Other ranks leave width/height at 0, i.e. no zero-copy.
                // Use the same heuristic as the OpenVINO backend (get_image_layout_from_shape): NHWC if the
                // last dim looks like channels, otherwise NCHW.
                if (shape.size() != 4)
                    continue;
                const bool is_nhwc = shape[3] <= 4 && shape[1] > 4;
                const size_t h = is_nhwc ? shape[1] : shape[2];
                const size_t w = is_nhwc ? shape[2] : shape[3];
                if (h && w) {
                    height = static_cast<uint32_t>(h);
                    width = static_cast<uint32_t>(w);
                    break;
                }
            }
        }

        if (width == 0 || height == 0)
            return false;

        // Aligned width: the DMA-BUF rows need no padding, so the NPU gets a plain contiguous tensor and
        // strided input support is not required (works on any NPU driver).
        if (alignDmaBufPitch(width) == width)
            return true;

        // Unaligned width: rows are padded to an aligned pitch, so the NPU must accept a strided ROI tensor.
        if (OpenVINOImageInference::IsNpuStridedInputSupported()) {
            GVA_INFO("NPU zero-copy: model input width %u is not %u-byte aligned, using padded rows with strided "
                     "NPU input",
                     width, DMA_BUF_PITCH_ALIGNMENT);
            return true;
        }
        GVA_WARNING("NPU zero-copy: model input width %u is not %u-byte aligned and the NPU driver does not "
                    "support strided input tensors",
                    width, DMA_BUF_PITCH_ALIGNMENT);
        return false;
    } catch (const std::exception &e) {
        GVA_WARNING("NPU zero-copy input check failed, zero-copy will be disabled: %s", e.what());
        return false;
    }
}
#endif

} // namespace

std::map<std::string, GstStructure *> ImageInference::GetModelInfoPreproc(const std::string model_file,
                                                                          const gchar *preproc_config,
                                                                          const gchar *ov_extension_lib) {
    return OpenVINOImageInference::GetModelInfoPreproc(model_file, preproc_config, ov_extension_lib);
}

std::map<std::string, std::vector<size_t>> ImageInference::GetModelInputShapes(const std::string model_file,
                                                                               const gchar *ov_extension_lib) {
    return OpenVINOImageInference::GetModelInputShapes(model_file, ov_extension_lib);
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
            // when the DMA-BUF heap is accessible and the NPU can consume the (possibly row-padded)
            // DMA-BUF layout; otherwise fall back to the plain SYSTEM-memory path. Both are checked here
            // (not later, per-surface) so the whole pipeline stays consistent: a partial fallback would
            // leave the pool/OpenVINO instance in DMA_BUFFER mode while individual surfaces are SYSTEM,
            // which deadlocks inference.
            // Enable zero-copy for NPU on Intel® Core™ Ultra Series 3 processor only
            if (isNpu && Utils::isCPUPTLHSeries(false)) {
                if (access("/dev/dma_heap/system", R_OK | W_OK) != 0) {
                    GVA_WARNING("Falling back to the slow NPU path (extra GPU->CPU->NPU copies): no access to "
                                "DMA-BUF. To enable zero-copy, grant access to /dev/dma_heap/system, e.g. "
                                "'sudo chgrp video /dev/dma_heap/system && sudo chmod 660 /dev/dma_heap/system'.");
                } else if (!isNpuZeroCopyInputSupported(config)) {
                    GVA_WARNING("Falling back to the slow NPU path (extra GPU->CPU->NPU copies): the model input "
                                "cannot be fed to the NPU as a zero-copy DMA-BUF. Use batch-size=1 and a "
                                "model input width that is a multiple of 64 or an NPU driver with strided input "
                                "support.");
                } else {
                    memory_type_to_use = MemoryType::DMA_BUFFER;
                    GVA_INFO("NPU zero-copy enabled.");
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