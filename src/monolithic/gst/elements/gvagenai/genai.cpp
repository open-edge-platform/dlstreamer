/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "genai.hpp"

#include <cmath>

GST_DEBUG_CATEGORY_EXTERN(gst_gvagenai_debug);
#define GST_CAT_DEFAULT gst_gvagenai_debug

#include "configs.hpp"

#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

namespace genai {

OpenVINOGenAIContext::OpenVINOGenAIContext(const std::string &model_path, const std::string &device,
                                           const std::string &cache_path, const std::string &generation_config_str,
                                           const std::string &scheduler_config_str) {
    // Initialize memory mapper for GStreamer buffers
    mapper = std::make_shared<dlstreamer::MemoryMapperGSTToCPU>(nullptr, nullptr);

    // Set configurations if provided
    if (!generation_config_str.empty()) {
        generation_config = ConfigParser::parse_generation_config_string(generation_config_str);
    }

    if (!scheduler_config_str.empty()) {
        scheduler_config = ConfigParser::parse_scheduler_config_string(scheduler_config_str);
    }

    ov::AnyMap properties;

    // Cache compiled models on disk for GPU to save time on the
    // next run. It's not beneficial for CPU.
    // TODO check NPU support for cache
    if (device.starts_with("GPU")) {
        properties.insert({ov::cache_dir(cache_path)});
    }

    if (get_scheduler_config()) {
        properties.insert({ov::genai::scheduler_config(*get_scheduler_config())});
    }

    GST_INFO("%s: %s", ov::get_openvino_version().description, ov::get_openvino_version().buildNumber);
    GST_INFO("Initializing OpenVINO™ GenAI VLM pipeline with model: %s on device: %s", model_path.c_str(),
             device.c_str());

    pipeline = std::make_unique<ov::genai::VLMPipeline>(model_path, device, properties);
    metrics = ov::genai::VLMPerfMetrics();
    GST_INFO("OpenVINO™ GenAI VLM pipeline initialized successfully");
}

OpenVINOGenAIContext::~OpenVINOGenAIContext() {
    tensor_vector.clear();
}

void OpenVINOGenAIContext::add_tensor_to_vector(GstBuffer *buffer, GstVideoInfo *info) {
    // Create a GSTFrame and map to CPU memory
    auto gst_frame = std::make_shared<dlstreamer::GSTFrame>(buffer, info);
    auto mapped_frame = mapper->map(gst_frame, dlstreamer::AccessMode::Read);

    // Convert to Mat, code from gvawatermark
    static constexpr std::array<int, 4> channels_to_cvtype_map = {CV_8UC1, CV_8UC2, CV_8UC3, CV_8UC4};
    std::vector<cv::Mat> image_planes;
    image_planes.reserve(mapped_frame->num_tensors());

    // Go through planes and create cv::Mat for every plane
    for (auto &tensor : *mapped_frame) {
        // Verify number of channels
        dlstreamer::ImageInfo image_info(tensor->info());
        assert(image_info.channels() > 0 && image_info.channels() <= channels_to_cvtype_map.size());
        const int cv_type = channels_to_cvtype_map[image_info.channels() - 1];
        image_planes.emplace_back(image_info.height(), image_info.width(), cv_type, tensor->data(),
                                  image_info.width_stride());
    }

    auto check_planes = [&image_planes](size_t n) {
        if (image_planes.size() != n)
            throw std::runtime_error("Image format error, plane count != " + std::to_string(n));
    };

    // Convert Mat to RGB format
    cv::Mat frame;
    switch (GST_VIDEO_INFO_FORMAT(info)) {
    case GST_VIDEO_FORMAT_RGB:
        check_planes(1);
        frame = image_planes[0];
        break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
        check_planes(1);
        cv::cvtColor(image_planes[0], frame, cv::COLOR_RGBA2RGB);
        break;
    case GST_VIDEO_FORMAT_BGR:
        check_planes(1);
        cv::cvtColor(image_planes[0], frame, cv::COLOR_BGR2RGB);
        break;
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
        check_planes(1);
        cv::cvtColor(image_planes[0], frame, cv::COLOR_BGRA2RGB);
        break;
    case GST_VIDEO_FORMAT_NV12: {
        check_planes(2);
        cv::cvtColorTwoPlane(image_planes[0], image_planes[1], frame, cv::COLOR_YUV2RGB_NV12);
        break;
    }
    case GST_VIDEO_FORMAT_I420: {
        check_planes(3);
        // For I420, need to create a single Mat with the layout Y+U+V
        uint8_t *y_data = image_planes[0].data;
        uint8_t *u_data = image_planes[1].data;
        uint8_t *v_data = image_planes[2].data;
        int y_size = image_planes[0].rows * image_planes[0].step;
        int u_size = image_planes[1].rows * image_planes[1].step;
        int v_size = image_planes[2].rows * image_planes[2].step;

        // Check if planes are contiguous
        if (u_data == y_data + y_size && v_data == u_data + u_size) {
            // Planes are contiguous (typical)
            cv::Mat yuv(info->height * 3 / 2, info->width, CV_8UC1, y_data);
            cv::cvtColor(yuv, frame, cv::COLOR_YUV2RGB_I420);
        } else {
            // Planes are not contiguous, need to copy (fallback)
            cv::Mat yuv(info->height * 3 / 2, info->width, CV_8UC1);
            image_planes[0].copyTo(yuv.rowRange(0, info->height));
            image_planes[1].copyTo(yuv.rowRange(info->height, info->height + info->height / 4));
            image_planes[2].copyTo(yuv.rowRange(info->height + info->height / 4, info->height * 3 / 2));
            cv::cvtColor(yuv, frame, cv::COLOR_YUV2RGB_I420);
        }
        break;
    }
    default:
        throw std::runtime_error("Unsupported video format: " + std::to_string(GST_VIDEO_INFO_FORMAT(info)));
    }

    // Create tensor
    auto tensor = ov::Tensor(ov::element::u8, {1, static_cast<size_t>(frame.rows), static_cast<size_t>(frame.cols),
                                               static_cast<size_t>(frame.channels())});
    size_t expected_size = frame.total() * frame.elemSize();
    if (tensor.get_byte_size() != expected_size) {
        throw std::runtime_error("Tensor size mismatch: expected " + std::to_string(expected_size) + ", got " +
                                 std::to_string(tensor.get_byte_size()));
    }
    memcpy(tensor.data(), frame.data, expected_size);

    // Add tensor to vector
    tensor_vector.push_back(tensor);
}

void OpenVINOGenAIContext::inference_tensor_vector(const std::string &prompt) {
    if (tensor_vector.empty()) {
        throw std::runtime_error("Tensor vector is empty");
    }

    ov::AnyMap properties = generation_config;

    // Set default max_new_tokens=100 if not specified
    if (properties.find(ov::genai::max_new_tokens.name()) == properties.end()) {
        properties.emplace(ov::genai::max_new_tokens(100));
    }

    // Add images to properties
    properties.emplace(ov::genai::images(tensor_vector));

    // Run inference, this is a long blocking call
    GST_INFO("Running inference with %ld images and prompt: %s", tensor_vector.size(), prompt.c_str());
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

    // Clear the tensor vector
    tensor_vector.clear();
}

size_t OpenVINOGenAIContext::get_tensor_vector_size() const {
    return tensor_vector.size();
}

void OpenVINOGenAIContext::clear_tensor_vector() {
    tensor_vector.clear();
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
