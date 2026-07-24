/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "genai.hpp"

#include <cmath>
#include <cstring>

GST_DEBUG_CATEGORY_EXTERN(gst_gvagenai_debug);
#define GST_CAT_DEFAULT gst_gvagenai_debug

#include "../../configs.hpp"

#include <openvino/genai/visual_language/video_metadata.hpp>

#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

namespace genai {

OpenVINOGenAIContext::OpenVINOGenAIContext(const std::string &model_path, const std::string &device,
                                           const std::string &cache_path, const std::string &generation_config_str,
                                           const std::string &scheduler_config_str,
                                           const std::string &pipeline_config_str) {
    // Set configurations if provided
    if (!generation_config_str.empty()) {
        generation_config = ConfigParser::parse_generation_config_string(generation_config_str);
    }

    if (!scheduler_config_str.empty()) {
        scheduler_config = ConfigParser::parse_scheduler_config_string(scheduler_config_str);
    }

    // Pipeline properties.
    ov::AnyMap properties = ConfigParser::parse_pipeline_config_string(pipeline_config_str);

    // Cache compiled models on disk for GPU and NPU to save time on the
    // next run. It's not beneficial for CPU.
    if (device.starts_with("GPU") || device.starts_with("NPU")) {
        properties[ov::cache_dir.name()] = cache_path;
    }

    if (get_scheduler_config()) {
        properties[ov::genai::scheduler_config.name()] = *get_scheduler_config();
    }

    GST_INFO("%s: %s", ov::get_openvino_version().description, ov::get_openvino_version().buildNumber);
    GST_INFO("Initializing OpenVINO™ GenAI VLM pipeline with model: %s on device: %s", model_path.c_str(),
             device.c_str());

    pipeline = std::make_unique<ov::genai::VLMPipeline>(model_path, device, properties);
    metrics = ov::genai::VLMPerfMetrics();
    GST_INFO("OpenVINO™ GenAI VLM pipeline initialized successfully");
}

OpenVINOGenAIContext::~OpenVINOGenAIContext() = default;

void OpenVINOGenAIContext::inference_tensor_vector(const std::vector<ov::Tensor> &frames, const std::string &prompt,
                                                   bool as_video, float fps) {
    if (frames.empty()) {
        throw std::runtime_error("Tensor vector is empty");
    }

    ov::AnyMap properties = generation_config;

    // Set default max_new_tokens=100 if not specified
    if (properties.find(ov::genai::max_new_tokens.name()) == properties.end()) {
        properties.emplace(ov::genai::max_new_tokens(100));
    }

    if (as_video) {
        // Present the accumulated frames as a single video clip. Each frame tensor is {1,H,W,C}
        // (see frame_utils.hpp); stack them into one {N,H,W,C} tensor as the video API expects.
        const ov::Shape frame_shape = frames.front().get_shape(); // {1, H, W, C}
        for (const auto &t : frames) {
            if (t.get_shape() != frame_shape) {
                throw std::runtime_error("Cannot build video tensor: frames have differing shapes. "
                                         "All frames in a chunk must share height, width and channels.");
            }
        }

        const size_t num_frames = frames.size();
        ov::Tensor video_tensor(ov::element::u8, {num_frames, frame_shape[1], frame_shape[2], frame_shape[3]});
        auto *dst = video_tensor.data<uint8_t>();
        const size_t frame_bytes = frames.front().get_byte_size();
        for (const auto &t : frames) {
            std::memcpy(dst, t.data(), frame_bytes);
            dst += frame_bytes;
        }

        ov::genai::VideoMetadata video_metadata;
        video_metadata.fps = fps; // 0.0 means unknown; the API handles this gracefully
        // Leave frames_indices empty: the upstream frame-rate property already pre-samples
        // frames, so defer any further sampling to the model's own logic.

        properties.emplace(ov::genai::videos(std::vector<ov::Tensor>{video_tensor}));
        properties.emplace(ov::genai::videos_metadata(std::vector<ov::genai::VideoMetadata>{video_metadata}));

        GST_INFO("Running inference with a %zu-frame video (fps=%.2f) and prompt: %s", num_frames, fps, prompt.c_str());
    } else {
        // Present the accumulated frames as independent images.
        properties.emplace(ov::genai::images(frames));
        GST_INFO("Running inference with %zu images and prompt: %s", frames.size(), prompt.c_str());
    }

    // Run inference, this is a long blocking call
    auto result = pipeline->generate(prompt, properties);
    GST_INFO("Inference completed successfully");

    // Store results and metrics
    last_result.clear();
    for (const auto &text : result.texts) {
        last_result += text;
    }

    // Extract confidence score from generation result.
    if (result.scores.empty()) {
        GST_INFO("No scores returned by VLM pipeline (scores vector empty), confidence unavailable");
        last_confidence = -1.0f;
    } else {
        const float raw_score = result.scores[0];
        GST_INFO("VLM scores vector size: %zu, raw score[0]: %f", result.scores.size(), raw_score);
        for (size_t i = 0; i < result.scores.size(); ++i) {
            GST_INFO("  score[%zu] = %f", i, result.scores[i]);
        }
        if (raw_score >= 0.0f) {
            // Greedy decoding: scores are filled with 0.0 by the API → not computed
            GST_INFO("Score is >= 0.0 (greedy decoding), confidence unavailable");
            last_confidence = -1.0f;
        } else {
            // Beam search or sampling: normalise by generated token count for per-token mean
            const auto num_tokens = static_cast<float>(result.perf_metrics.get_num_generated_tokens());
            const float divisor = (num_tokens > 0.0f) ? num_tokens : 1.0f;
            last_confidence = std::exp(raw_score / divisor);
            // Clamp to [0, 1]
            last_confidence = std::max(0.0f, std::min(1.0f, last_confidence));
            GST_INFO("Confidence (exp(%.4f / %.0f tokens)): %.4f", raw_score, divisor, last_confidence);
        }
    }

    // Update metrics
    if (metrics.load_time == 0) {
        metrics = result.perf_metrics;
    } else {
        metrics += result.perf_metrics;
    }
}

void OpenVINOGenAIContext::set_generation_config(const std::string &config_str) {
    generation_config = ConfigParser::parse_generation_config_string(config_str);
}

void OpenVINOGenAIContext::set_scheduler_config(const std::string &config_str) {
    scheduler_config = ConfigParser::parse_scheduler_config_string(config_str);
}

ov::AnyMap OpenVINOGenAIContext::get_generation_config() const {
    return generation_config;
}

std::optional<ov::genai::SchedulerConfig> OpenVINOGenAIContext::get_scheduler_config() const {
    return scheduler_config;
}

std::string OpenVINOGenAIContext::get_last_result() const {
    return last_result;
}

float OpenVINOGenAIContext::get_last_confidence() const {
    return last_confidence;
}

std::string OpenVINOGenAIContext::create_json_metadata(GstClockTime timestamp, bool include_metrics) {
    auto round_2dp = [](double value) { return std::round(value * 100.0) / 100.0; };

    nlohmann::ordered_json json_obj = {{"result", last_result}};
    if (last_confidence >= 0.0f) {
        json_obj["confidence"] = round_2dp(last_confidence);
    }
    if (include_metrics) {
        nlohmann::ordered_json metrics_obj = {
            {"load_time", round_2dp(metrics.get_load_time())},
            {"num_generated_tokens", metrics.get_num_generated_tokens()},
            {"num_input_tokens", metrics.get_num_input_tokens()},
            {"inference_duration_mean", round_2dp(metrics.get_inference_duration().mean)},
            {"inference_duration_std", round_2dp(metrics.get_inference_duration().std)},
            {"generate_duration_mean", round_2dp(metrics.get_generate_duration().mean)},
            {"generate_duration_std", round_2dp(metrics.get_generate_duration().std)},
            {"tokenization_duration_mean", round_2dp(metrics.get_tokenization_duration().mean)},
            {"tokenization_duration_std", round_2dp(metrics.get_tokenization_duration().std)},
            {"detokenization_duration_mean", round_2dp(metrics.get_detokenization_duration().mean)},
            {"detokenization_duration_std", round_2dp(metrics.get_detokenization_duration().std)},
            {"chat_template_duration_mean", round_2dp(metrics.get_chat_template_duration().mean)},
            {"chat_template_duration_std", round_2dp(metrics.get_chat_template_duration().std)},
            {"prepare_embeddings_duration_mean", round_2dp(metrics.get_prepare_embeddings_duration().mean)},
            {"prepare_embeddings_duration_std", round_2dp(metrics.get_prepare_embeddings_duration().std)},
            {"ttft_mean", round_2dp(metrics.get_ttft().mean)},
            {"ttft_std", round_2dp(metrics.get_ttft().std)},
            {"tpot_mean", round_2dp(metrics.get_tpot().mean)},
            {"tpot_std", round_2dp(metrics.get_tpot().std)},
            {"ipot_mean", round_2dp(metrics.get_ipot().mean)},
            {"ipot_std", round_2dp(metrics.get_ipot().std)},
            {"throughput_mean", round_2dp(metrics.get_throughput().mean)},
            {"throughput_std", round_2dp(metrics.get_throughput().std)}};
        json_obj["metrics"] = metrics_obj;
    }
    if (GST_CLOCK_TIME_IS_VALID(timestamp)) {
        json_obj["timestamp"] = timestamp;
        json_obj["timestamp_seconds"] = round_2dp((double)timestamp / GST_SECOND);
    }
    return json_obj.dump();
}

} // namespace genai
