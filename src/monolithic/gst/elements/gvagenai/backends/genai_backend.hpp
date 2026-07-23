/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>

#include "genai_config.h"

#include <map>
#include <memory>
#include <mutex>
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
 * @brief Abstract interface for all GenAI backends
 *
 * Uses a two-phase model: frames are accumulated via add_frame() and then
 * processed together by infer(). Each backend owns how it stores frames,
 * allowing it to map the GStreamer buffer to the memory type it prefers
 * (CPU cv::Mat today; VAAPI surface / D3D11 texture / DMABuf in the future)
 * without a forced GPU->CPU download at the interface boundary.
 */
class IGenAIBackend {
  public:
    virtual ~IGenAIBackend() = default;

    /**
     * @brief Accumulate a single frame for the next inference
     *
     * The backend decides how to map and store the buffer (CPU/VAAPI/D3D11).
     * @param buffer GStreamer buffer containing the video frame
     * @param info Video format information
     * @throws std::runtime_error if the frame cannot be mapped/converted
     */
    virtual void add_frame(GstBuffer *buffer, GstVideoInfo *info) = 0;

    /**
     * @brief Number of frames currently accumulated
     * @return Count of buffered frames awaiting inference
     */
    virtual size_t frame_count() const = 0;

    /**
     * @brief Discard all accumulated frames without running inference
     */
    virtual void clear_frames() = 0;

    /**
     * @brief Run inference on the accumulated frames
     *
     * On success the backend clears its internal frame buffer.
     * @param prompt Text query/prompt
     * @param as_video If true, present the accumulated frames as a single video clip
     *        instead of independent images (backend-specific; ignored if unsupported).
     * @param fps Frame rate of the accumulated frames, used when as_video is true.
     *        Pass 0.0 if unknown. Ignored when as_video is false.
     * @param timestamp Buffer timestamp to bake into the result's raw_json
     * @return GenAIResult with text, confidence, and metadata
     * @throws std::runtime_error on failure
     */
    virtual GenAIResult infer(const std::string &prompt, bool as_video = false, float fps = 0.0f,
                              GstClockTime timestamp = GST_CLOCK_TIME_NONE) = 0;

    /**
     * @brief Update generation configuration (backend-specific format)
     * @param cfg Configuration string
     */
    virtual void set_generation_config(const std::string &cfg) = 0;

    /**
     * @brief Get backend identifier for logging/debugging
     * @return Backend name (e.g., "openvino", "openai-http")
     */
    virtual std::string backend_id() const = 0;

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
    std::string server_url; // e.g., "http://localhost:8000/v1"
    std::string model_name; // e.g., "llava-1.5-7b"
    std::string api_key;    // Optional: Bearer token or API key
    std::string timeout_ms; // Optional: request timeout
};

/**
 * @brief Process-wide registry for GenAI backends
 *
 * Thread-safe singleton that manages backend lifecycle:
 * - OpenVINO backends: new instance per request (no caching)
 * - HTTP backends: cached by (server_url, model) pair (shared reuse)
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
     * @brief Create (or reuse) a backend from a plain-C config
     *
     * Dispatches on config.backend ("openvino" / "openai-http"). OpenVINO
     * backends are always freshly created (per-element ownership); HTTP
     * backends are cached and shared by (server_url, model).
     *
     * @param config Backend configuration (element property storage)
     * @return Shared pointer to the backend
     * @throws std::runtime_error if the backend type is unknown or creation fails
     */
    std::shared_ptr<IGenAIBackend> create_backend(const GenAIBackendConfig &config);

    /**
     * @brief Clear all cached HTTP backends
     *
     * Useful for testing or graceful shutdown. OpenVINO backends
     * are not cached so this doesn't affect them.
     */
    void clear_http_backends();

    /**
     * @brief Get count of currently cached HTTP backends
     * @return Number of cached HTTP backends
     */
    size_t http_backend_count() const;

  private:
    GenAIBackendRegistry() = default;
    ~GenAIBackendRegistry() = default;

    // Prevent copying
    GenAIBackendRegistry(const GenAIBackendRegistry &) = delete;
    GenAIBackendRegistry &operator=(const GenAIBackendRegistry &) = delete;

    // Backend-specific creation helpers (called by create_backend)
    std::shared_ptr<IGenAIBackend> get_openvino_backend(const OpenVINOBackendParams &params);
    std::shared_ptr<IGenAIBackend> get_http_backend(const HttpBackendParams &params);

    // Cache key: (server_url, model_name)
    using HttpBackendKey = std::pair<std::string, std::string>;

    mutable std::mutex mutex_;
    std::map<HttpBackendKey, std::shared_ptr<IGenAIBackend>> http_backends_;
};

} // namespace genai