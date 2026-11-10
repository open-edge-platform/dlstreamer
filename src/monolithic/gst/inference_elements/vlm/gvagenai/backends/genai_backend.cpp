/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "genai_backend.hpp"
#include "openvino-genai/openvino_genai_backend.hpp"
#ifdef GVAGENAI_HAVE_HTTP_BACKEND
#include "openai-http/openai_http_backend.hpp"
#endif

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

    if (backend.empty() || backend == "openvino-genai") {
        OpenVINOBackendParams params;
        params.model_path = str(config.model);
        params.device = str(config.device);
        params.cache_path = str(config.cache_path);
        params.generation_config = str(config.generation_config);
        params.scheduler_config = str(config.scheduler_config);
        params.pipeline_config = str(config.pipeline_config);
        params.include_metrics = (config.include_metrics != FALSE);
        params.model_instance_id = str(config.model_instance_id);
        return get_openvino_backend(params);
    }

    if (backend == "openai-http") {
        HttpBackendParams params;
        params.server_url = str(config.server_url);
        params.model_name = str(config.model);
        params.api_key = str(config.api_key);
        params.timeout_ms = str(config.timeout_ms);
        params.generation_config = str(config.generation_config);
        params.include_metrics = (config.include_metrics != FALSE);
        return get_http_backend(params);
    }

    throw std::runtime_error("Unknown backend '" + backend + "' (valid: 'openvino-genai', 'openai-http')");
}

std::shared_ptr<IGenAIBackend> GenAIBackendRegistry::get_openvino_backend(const OpenVINOBackendParams &params) {
    auto build_backend = [&params]() -> std::shared_ptr<IGenAIBackend> {
        try {
            return std::make_shared<OpenVINOGenAIBackend>(params);
        } catch (const std::exception &e) {
            GST_ERROR("Failed to create OpenVINO backend: %s", e.what());
            throw;
        }
    };

    if (params.model_instance_id.empty()) {
        // No model-instance-id set: always build a fresh, isolated instance and skip the
        // cache entirely (neither looked up nor registered for other elements to reuse).
        GST_INFO("Creating isolated OpenVINO backend (no model-instance-id): model=%s, device=%s",
                 params.model_path.c_str(), params.device.c_str());
        return build_backend();
    }

    // Elements sharing the same model-instance-id share one backend instance, matching the
    // convention used by gvadetect/gvaclassify. Callers are responsible for using the same
    // instance-id only across elements with matching model configuration.
    const std::string &key = params.model_instance_id;

    auto try_get_cached = [this](const std::string &cache_key) -> std::shared_ptr<IGenAIBackend> {
        auto cached = openvino_backends_.find(cache_key);
        if (cached == openvino_backends_.end()) {
            return nullptr;
        }
        if (auto backend = cached->second.lock()) {
            return backend;
        }
        openvino_backends_.erase(cached);
        return nullptr;
    };

    std::shared_ptr<std::mutex> key_mutex;
    {
        std::lock_guard<std::mutex> lock(openvino_backends_mutex_);

        for (auto it = openvino_backends_.begin(); it != openvino_backends_.end();) {
            if (it->second.expired()) {
                it = openvino_backends_.erase(it);
            } else {
                ++it;
            }
        }

        if (auto backend = try_get_cached(key)) {
            GST_INFO("Reusing shared OpenVINO backend for model-instance-id '%s': model=%s, device=%s", key.c_str(),
                     params.model_path.c_str(), params.device.c_str());
            return backend;
        }

        auto &slot = openvino_backend_key_mutexes_[key];
        if (!slot) {
            slot = std::make_shared<std::mutex>();
        }
        key_mutex = slot;
    }

    std::lock_guard<std::mutex> key_lock(*key_mutex);

    {
        std::lock_guard<std::mutex> lock(openvino_backends_mutex_);
        if (auto backend = try_get_cached(key)) {
            GST_INFO("Reusing shared OpenVINO backend for model-instance-id '%s': model=%s, device=%s", key.c_str(),
                     params.model_path.c_str(), params.device.c_str());
            return backend;
        }
    }

    GST_INFO("Creating shared OpenVINO backend for model-instance-id '%s': model=%s, device=%s", key.c_str(),
             params.model_path.c_str(), params.device.c_str());
    auto backend = build_backend();

    {
        std::lock_guard<std::mutex> lock(openvino_backends_mutex_);
        openvino_backends_[key] = backend;
    }
    return backend;
}

std::shared_ptr<IGenAIBackend> GenAIBackendRegistry::get_http_backend(const HttpBackendParams &params) {
#ifdef GVAGENAI_HAVE_HTTP_BACKEND
    // Always create a fresh instance (see class documentation: HTTP backends are never
    // shared/cached because IGenAIBackend accumulates per-element frame state).
    GST_INFO("Creating new HTTP backend for %s (model: %s)", params.server_url.c_str(), params.model_name.c_str());

    try {
        return std::make_shared<OpenAIHttpBackend>(params);
    } catch (const std::exception &e) {
        GST_ERROR("Failed to create HTTP backend: %s", e.what());
        throw;
    }
#else
    (void)params;
    throw std::runtime_error("'openai-http' backend is not available: DL Streamer was built without libcurl");
#endif
}

} // namespace genai
