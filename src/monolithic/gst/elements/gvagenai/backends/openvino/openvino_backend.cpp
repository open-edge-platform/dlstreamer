/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "openvino_backend.hpp"

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

GenAIResult OpenVINOGenAIBackend::infer(const std::string &prompt, bool as_video, float fps) {
    if (context_->get_tensor_vector_size() == 0) {
        throw std::runtime_error("Cannot run inference with no accumulated frames");
    }

    // Run inference (clears tensor vector internally on success)
    try {
        context_->inference_tensor_vector(prompt, as_video, fps);
    } catch (const std::exception &e) {
        context_->clear_tensor_vector();
        throw std::runtime_error(std::string("OpenVINO inference failed: ") + e.what());
    }

    // Assemble result
    GenAIResult result;
    result.text = context_->get_last_result();
    result.confidence = context_->get_last_confidence();
    result.raw_json = context_->create_json_metadata(GST_CLOCK_TIME_NONE, include_metrics_);
    result.backend_name = backend_id();

    return result;
}

void OpenVINOGenAIBackend::add_frame(GstBuffer *buffer, GstVideoInfo *info) {
    context_->add_tensor_to_vector(buffer, info);
}

size_t OpenVINOGenAIBackend::frame_count() const {
    return context_->get_tensor_vector_size();
}

void OpenVINOGenAIBackend::clear_frames() {
    context_->clear_tensor_vector();
}

void OpenVINOGenAIBackend::set_generation_config(const std::string &cfg) {
    context_->set_generation_config(cfg);
}

} // namespace genai
