/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "openvino_backend.hpp"

#include <future>
#include <gst/gst.h>
#include <stdexcept>

GST_DEBUG_CATEGORY_EXTERN(gst_gvagenai_debug);
#define GST_CAT_DEFAULT gst_gvagenai_debug

namespace genai {

OpenVINOGenAIBackend::OpenVINOGenAIBackend(const OpenVINOBackendParams &params)
    : include_metrics_(params.include_metrics) {
    try {
        context_ = std::make_unique<OpenVINOGenAIContext>(params.model_path, params.device, params.cache_path,
                                                          params.generation_config, params.scheduler_config,
                                                          params.pipeline_config);
    } catch (const std::exception &e) {
        throw std::runtime_error(std::string("Failed to initialize OpenVINO backend: ") + e.what());
    }
}

std::future<GenAIResult> OpenVINOGenAIBackend::submit(GenRequest req) {
    std::promise<GenAIResult> promise;
    std::future<GenAIResult> future = promise.get_future();

    // Synchronous backend: compute now and return an already-satisfied future.
    try {
        if (req.frames.empty()) {
            throw std::runtime_error("Cannot run inference with no accumulated frames");
        }

        context_->inference_tensor_vector(req.frames, req.prompt, req.as_video, req.fps);

        GenAIResult result;
        result.text = context_->get_last_result();
        result.confidence = context_->get_last_confidence();
        result.raw_json = context_->create_json_metadata(req.timestamp, include_metrics_);
        result.backend_name = describe();

        promise.set_value(std::move(result));
    } catch (const std::exception &e) {
        promise.set_exception(
            std::make_exception_ptr(std::runtime_error(std::string("OpenVINO inference failed: ") + e.what())));
    }

    return future;
}

void OpenVINOGenAIBackend::set_generation_config(const std::string &cfg) {
    context_->set_generation_config(cfg);
}

} // namespace genai
