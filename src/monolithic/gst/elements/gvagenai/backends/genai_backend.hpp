/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>

#include "genai_config.h"

#include <openvino/runtime/tensor.hpp>

#include <future>
#include <memory>
#include <string>
#include <vector>

namespace genai {

/**
 * @brief Result from a GenAI inference call
 */
struct GenAIResult {
    std::string text;         // Model output text
    float confidence;         // Confidence: [0.0-1.0] or -1.0 if unavailable
    std::string raw_json;     // Backend-specific metadata (e.g., OpenVINO metrics, HTTP response)
    std::string backend_name; // Identifier of backend that produced this result
};

/**
 * @brief A single, self-contained inference request
 *
 * Carries everything one inference needs, so backends hold no per-inference
 * frame state and multiple requests can be built/submitted independently.
 * Frames are RGB u8 tensors of shape {1, H, W, C}, already mapped to CPU by
 * the element (see frame_utils.hpp). Tensors own their pixel data so the
 * originating GstBuffer can be released immediately after the request is built.
 */
struct GenRequest {
    std::vector<ov::Tensor> frames;               // RGB {1,H,W,C} owning tensors
    std::string prompt;                           // Text query/prompt
    bool as_video = false;                        // Present frames as one video clip vs independent images
    float fps = 0.0f;                             // Frame rate of the clip (used when as_video); 0 if unknown
    GstClockTime timestamp = GST_CLOCK_TIME_NONE; // Buffer timestamp to bake into the result's raw_json
};

/**
 * @brief Abstract interface for all GenAI backends
 *
 * Stateless per request: an inference is fully described by the GenRequest
 * passed to submit(). Backends own how they turn the RGB tensors into their
 * native input (OpenVINO tensors, base64 JPEG, ...).
 */
class IGenAIBackend {
  public:
    using Ptr = std::shared_ptr<IGenAIBackend>;

    virtual ~IGenAIBackend() = default;

    /**
     * @brief Run inference on the request's frames
     *
     * Returns a future that is already satisfied for synchronous backends, or
     * becomes ready later for asynchronous ones. get() returns the GenAIResult
     * or rethrows any error raised during generation.
     */
    virtual std::future<GenAIResult> submit(GenRequest req) = 0;

    /**
     * @brief Update generation configuration (backend-specific format)
     * @param cfg Configuration string
     */
    virtual void set_generation_config(const std::string &cfg) = 0;

    /**
     * @brief Get backend identifier for logging/debugging and JSON meta
     * @return Backend name (e.g., "openvino", "openai-http")
     */
    virtual std::string describe() const = 0;

    /**
     * @brief Check if backend is asynchronous
     * @return true if backend performs async operations
     */
    virtual bool is_async() const {
        return false;
    }
};

/**
 * @brief Parameters passed to the OpenVINO backend constructor (internal)
 */
struct OpenVINOBackendParams {
    std::string model_path;
    std::string device; // "CPU", "GPU", "NPU"
    std::string cache_path;
    std::string generation_config;
    std::string scheduler_config;
    std::string pipeline_config;  // OpenVINO device properties passed at pipeline construction
    bool include_metrics = false; // Include performance metrics in JSON output
};

/**
 * @brief Parameters passed to the HTTP backend constructor (internal)
 */
struct HttpBackendParams {
    std::string server_url;       // e.g., "http://localhost:8000/v1"
    std::string model_name;       // e.g., "llava-1.5-7b"
    std::string api_key;          // Optional: Bearer token or API key
    std::string timeout_ms;       // Optional: request timeout
    bool include_metrics = false; // Include usage/token metrics in JSON output
};

/**
 * @brief Process-wide registry for GenAI backends
 *
 * Factory for backend instances. Every call to create_backend() returns a
 * freshly created backend (OpenVINO and HTTP alike) with per-element
 * ownership: IGenAIBackend accumulates frame state internally (add_frame/
 * frame_count/clear_frames), so instances must never be shared across
 * multiple gvagenai elements - doing so would corrupt each other's frame
 * buffers. No caching/sharing is performed by design.
 *
 * Example usage:
 *   GenAIBackendConfig cfg = {};
 *   cfg.backend = (gchar *)"openai-http";
 *   cfg.server_url = (gchar *)"http://localhost:8000/v1";
 *   cfg.model = (gchar *)"llava-1.5-7b";
 *   auto backend = GenAIBackendRegistry::instance().create_backend(cfg);
 */
class GenAIBackendRegistry {
  public:
    /**
     * @brief Get process-wide registry singleton
     */
    static GenAIBackendRegistry &instance();

    /**
     * @brief Create a new backend from a plain-C config
     *
     * Dispatches on config.backend ("openvino" / "openai-http"). Always
     * creates a fresh instance - see class documentation for why backends
     * are never shared/cached.
     *
     * @param config Backend configuration (element property storage)
     * @return Shared pointer to the backend
     * @throws std::runtime_error if the backend type is unknown or creation fails
     */
    std::shared_ptr<IGenAIBackend> create_backend(const GenAIBackendConfig &config);

  private:
    GenAIBackendRegistry() = default;
    ~GenAIBackendRegistry() = default;

    // Prevent copying
    GenAIBackendRegistry(const GenAIBackendRegistry &) = delete;
    GenAIBackendRegistry &operator=(const GenAIBackendRegistry &) = delete;

    // Backend-specific creation helpers (called by create_backend)
    std::shared_ptr<IGenAIBackend> get_openvino_backend(const OpenVINOBackendParams &params);
    std::shared_ptr<IGenAIBackend> get_http_backend(const HttpBackendParams &params);
};

} // namespace genai