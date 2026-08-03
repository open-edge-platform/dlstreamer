/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "../genai_backend.hpp"

#include <future>
#include <string>

namespace genai {

/**
 * @brief OpenAI-compatible HTTP backend adapter
 *
 * Talks to any server exposing an OpenAI Chat Completions API with vision
 * support (vLLM, OVMS, Ollama, LM Studio, OpenAI, Azure OpenAI, ...). Frames
 * are accumulated as base64-encoded JPEG data URLs and sent as image_url
 * content parts alongside the text prompt in a single /chat/completions
 * request.
 *
 * Per-element instance (see GenAIBackendRegistry: never shared/cached).
 * "Video" presentation (as_video=true) is not supported by the Chat
 * Completions API; frames are always sent as independent images.
 */
class OpenAIHttpBackend : public IGenAIBackend {
  public:
    /**
     * @brief Construct the HTTP backend
     * @param params Server URL, model name, optional API key/timeout
     */
    explicit OpenAIHttpBackend(const HttpBackendParams &params);

    ~OpenAIHttpBackend() override;

    /**
     * @brief Send the request's frames + prompt to the server's /chat/completions endpoint
     *
     * Frames are JPEG-encoded and base64-embedded as image_url content parts. The Chat
     * Completions API has no video content type, so req.as_video is ignored (a warning is
     * logged) and frames are always sent as independent images. Returns an already-satisfied
     * future carrying the parsed result, or an exception on failure.
     */
    std::future<GenAIResult> submit(GenRequest req) override;

    /**
     * @brief Update generation configuration
     *
     * Format: KEY=VALUE,KEY=VALUE (e.g. "max_tokens=200,temperature=0.2"). Keys are
     * passed through as top-level fields in the JSON request body, so any field the
     * target server's Chat Completions API accepts can be set this way.
     */
    void set_generation_config(const std::string &cfg) override;

    /**
     * @brief Backend identifier
     */
    std::string describe() const override {
        return "openai-http";
    }

  private:
    std::string chat_completions_url_;
    std::string model_name_;
    std::string api_key_;
    long timeout_ms_;
    bool include_metrics_;

    std::string generation_config_; // raw KEY=VALUE,KEY=VALUE string
};

} // namespace genai
