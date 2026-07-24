/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "../genai_backend.hpp"
#include "genai.hpp"

#include <memory>
#include <string>

namespace genai {

/**
 * @brief OpenVINO GenAI backend adapter
 *
 * Implements IGenAIBackend by wrapping the low-level OpenVINOGenAIContext.
 *
 */
class OpenVINOGenAIBackend : public IGenAIBackend {
  public:
    /**
     * @brief Construct the OpenVINO backend
     * @param params OpenVINO configuration (model path, device, cache, configs)
     * @throws std::runtime_error if model loading fails
     */
    explicit OpenVINOGenAIBackend(const OpenVINOBackendParams &params);

    ~OpenVINOGenAIBackend() override = default;

    /**
     * @brief Run inference on the request's frame tensors
     *
     * Returns an already-satisfied future carrying text, confidence, and full
     * JSON metadata (with metrics and timestamp), or an exception on failure.
     */
    std::future<GenAIResult> submit(GenRequest req) override;

    /**
     * @brief Update generation configuration (KEY=VALUE,KEY=VALUE format)
     */
    void set_generation_config(const std::string &cfg) override;

    /**
     * @brief Backend identifier
     */
    std::string describe() const override {
        return "openvino";
    }

  private:
    std::unique_ptr<OpenVINOGenAIContext> context_;
    bool include_metrics_ = false;
};

} // namespace genai
