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
     * @brief Accumulate a frame as an OpenVINO tensor (GstBuffer -> RGB -> tensor)
     */
    void add_frame(GstBuffer *buffer, GstVideoInfo *info) override;

    /**
     * @brief Number of accumulated tensors
     */
    size_t frame_count() const override;

    /**
     * @brief Clear accumulated tensors
     */
    void clear_frames() override;

    /**
     * @brief Run inference on accumulated tensors
     *
     * Returns text, confidence, and full JSON metadata (with metrics).
     */
    GenAIResult infer(const std::string &prompt) override;

    /**
     * @brief Update generation configuration (KEY=VALUE,KEY=VALUE format)
     */
    void set_generation_config(const std::string &cfg) override;

    /**
     * @brief Backend identifier
     */
    std::string backend_id() const override {
        return "openvino";
    }

  private:
    std::unique_ptr<OpenVINOGenAIContext> context_;
    bool include_metrics_ = false;
};

} // namespace genai
