/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "genai_backend.hpp"
#include "openvino/openvino_backend.hpp"

#include <gst/gst.h>
#include <stdexcept>

namespace genai {

// Singleton instance
GenAIBackendRegistry &GenAIBackendRegistry::instance() {
    static GenAIBackendRegistry registry;
    return registry;
}

std::shared_ptr<IGenAIBackend> GenAIBackendRegistry::create_backend(const GenAIBackendConfig &config) {
    // Helper: NULL gchar* -> empty string
    auto str = [](const gchar *s) { return std::string(s ? s : ""); };

    const std::string backend = str(config.backend);

    if (backend.empty() || backend == "openvino") {
        OpenVINOBackendParams params;
        params.model_path = str(config.model);
        params.device = str(config.device);
        params.cache_path = str(config.cache_path);
        params.generation_config = str(config.generation_config);
        params.scheduler_config = str(config.scheduler_config);
        params.include_metrics = (config.include_metrics != FALSE);
        return get_openvino_backend(params);
    }

    if (backend == "openai-http") {
        HttpBackendParams params;
        params.server_url = str(config.server_url);
        params.model_name = str(config.model);
        params.api_key = str(config.api_key);
        params.timeout_ms = str(config.timeout_ms);
        return get_http_backend(params);
    }

    throw std::runtime_error("Unknown backend '" + backend + "' (valid: 'openvino', 'openai-http')");
}

std::shared_ptr<IGenAIBackend> GenAIBackendRegistry::get_openvino_backend(const OpenVINOBackendParams &params) {
    // OpenVINO backends are always newly created (not cached)
    // Each element gets its own instance for per-element model ownership
    GST_INFO("Creating new OpenVINO backend: model=%s, device=%s", params.model_path.c_str(), params.device.c_str());

    try {
        return std::make_shared<OpenVINOGenAIBackend>(params);
    } catch (const std::exception &e) {
        GST_ERROR("Failed to create OpenVINO backend: %s", e.what());
        throw;
    }
}

std::shared_ptr<IGenAIBackend> GenAIBackendRegistry::get_http_backend(const HttpBackendParams &params) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Create cache key from server_url and model_name
    HttpBackendKey key = {params.server_url, params.model_name};

    // Check if already cached
    auto it = http_backends_.find(key);
    if (it != http_backends_.end()) {
        GST_DEBUG("Reusing cached HTTP backend for %s (model: %s)", params.server_url.c_str(),
                  params.model_name.c_str());
        return it->second;
    }

    // Create new HTTP backend
    GST_INFO("Creating new HTTP backend for %s (model: %s)", params.server_url.c_str(), params.model_name.c_str());

    try {
        // TODO: Implement OpenAIHttpBackend
        // For now, placeholder
        throw std::runtime_error("HTTP backend not yet implemented");

        // auto backend = std::make_shared<OpenAIHttpBackend>(
        //     params.server_url, params.model_name, params.api_key, params.timeout_ms);
        //
        // http_backends_[key] = backend;
        // GST_INFO("Cached HTTP backend, total backends now: %zu", http_backends_.size());
        // return backend;
    } catch (const std::exception &e) {
        GST_ERROR("Failed to create HTTP backend: %s", e.what());
        throw;
    }
}

void GenAIBackendRegistry::clear_http_backends() {
    std::lock_guard<std::mutex> lock(mutex_);
    GST_INFO("Clearing %zu cached HTTP backends", http_backends_.size());
    http_backends_.clear();
}

size_t GenAIBackendRegistry::http_backend_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return http_backends_.size();
}

} // namespace genai
