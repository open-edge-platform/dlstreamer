/*******************************************************************************
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>

#include "dlstreamer/gst/mappers/gst_to_cpu.h"

#include <openvino/genai/visual_language/pipeline.hpp>

namespace genai {

/**
 * @brief OpenVINO™ GenAI context
 */
class OpenVINOGenAIContext {
  public:
    /**
     * @brief Initialize the OpenVINO™ GenAI pipeline
     * @param model_path Path to the model
     * @param device Target device (CPU, GPU, NPU, etc.)
     * @param cache_path Path for caching compiled models (used only for GPU)
     * @param generation_config_str Optional generation configuration string in KEY=VALUE,KEY=VALUE format
     * @param scheduler_config_str Optional scheduler configuration string in KEY=VALUE,KEY=VALUE format
     * @throws std::exception if initialization fails
     */
    OpenVINOGenAIContext(const std::string &model_path, const std::string &device,
                         const std::string &cache_path = "ov_cache", const std::string &generation_config_str = "",
                         const std::string &scheduler_config_str = "");
    ~OpenVINOGenAIContext();

    /**
     * @brief Convert GStreamer buffer to tensor and add to vector
     * @param buffer GStreamer buffer containing video frame
     * @param info Video format information
     * @throws std::runtime_error if conversion fails
     */
    void add_tensor_to_vector(GstBuffer *buffer, GstVideoInfo *info);

    /**
     * @brief Run inference on buffered tensors
     * @param prompt Text prompt for the model
     * @throws std::runtime_error if inference fails
     */
    void inference_tensor_vector(const std::string &prompt);

    /**
     * @brief Get number of tensors in the vector
     * @return Number of tensors
     */
    size_t get_tensor_vector_size() const;

    /**
     * @brief Clear the tensor vector
     */
    void clear_tensor_vector();

    /**
     * @brief Set generation configuration from string
     * @param config_str Configuration string in KEY=VALUE,KEY=VALUE format
     */
    void set_generation_config(const std::string &config_str);

    /**
     * @brief Set scheduler configuration from string
     * @param config_str Configuration string in KEY=VALUE,KEY=VALUE format
     */
    void set_scheduler_config(const std::string &config_str);

    /**
     * @brief Get the generation configuration map
     * @return AnyMap with generation properties
     */
    ov::AnyMap get_generation_config() const;

    /**
     * @brief Get the scheduler configuration
     * @return SchedulerConfig object
     */
    std::optional<ov::genai::SchedulerConfig> get_scheduler_config() const;

    /**
     * @brief Get the last inference result
     * @return Result text
     */
    std::string get_last_result() const;

    /**
     * @brief Get the confidence score from the last inference.
     * For beam search / sampling: per-token geometric mean probability in [0, 1].
     * For greedy decoding (scores filled with 0 by the API): returns -1.0f (unavailable).
     * @return Confidence in [0.0, 1.0], or -1.0f if unavailable.
     */
    float get_last_confidence() const;

    /**
     * @brief Create JSON metadata from the last result and metrics
     * @param timestamp Frame timestamp in nanoseconds (GST_CLOCK_TIME)
     * @param include_metrics Whether to include performance metrics
     * @return JSON string containing result, performance metrics, and frame timestamp
     */
    std::string create_json_metadata(GstClockTime timestamp = GST_CLOCK_TIME_NONE, bool include_metrics = false);

  private:
    std::shared_ptr<dlstreamer::MemoryMapperGSTToCPU> mapper = nullptr;
    std::unique_ptr<ov::genai::VLMPipeline> pipeline = nullptr;
    ov::AnyMap generation_config = {};
    std::optional<ov::genai::SchedulerConfig> scheduler_config = std::nullopt;
    ov::genai::VLMPerfMetrics metrics = {};
    std::string last_result = "";
    float last_confidence = -1.0f; // -1.0f = unavailable (greedy decoding fills scores with 0)
    std::vector<ov::Tensor> tensor_vector = {};
};

} // namespace genai
