/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "openai_http_backend.hpp"

#include <curl/curl.h>
#include <gst/gst.h>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

#include <cmath>
#include <future>
#include <mutex>
#include <sstream>
#include <stdexcept>

GST_DEBUG_CATEGORY_EXTERN(gst_gvagenai_debug);
#define GST_CAT_DEFAULT gst_gvagenai_debug

namespace genai {

namespace {

// libcurl requires curl_global_init() once per process before any easy handle is used.
void ensure_curl_global_init() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// Wrap an RGB {1,H,W,C} u8 tensor as a BGR cv::Mat (BGR is what cv::imencode expects).
cv::Mat rgb_tensor_to_bgr_mat(const ov::Tensor &tensor) {
    const ov::Shape shape = tensor.get_shape(); // {1, H, W, C}
    if (shape.size() != 4 || shape[0] != 1 || shape[3] != 3) {
        throw std::runtime_error("HTTP backend expects RGB frame tensors of shape {1, H, W, 3}");
    }
    const int height = static_cast<int>(shape[1]);
    const int width = static_cast<int>(shape[2]);

    // Wrap the tensor data without copying; cvtColor only reads it.
    cv::Mat rgb(height, width, CV_8UC3, const_cast<void *>(tensor.data()));
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    return bgr;
}

// Minimal RFC 4648 base64 encoder (no external dependency).
std::string base64_encode(const std::vector<uchar> &data) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6) & 0x3F];
        out += table[n & 0x3F];
        i += 3;
    }

    const size_t remaining = data.size() - i;
    if (remaining == 1) {
        uint32_t n = data[i] << 16;
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += "==";
    } else if (remaining == 2) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6) & 0x3F];
        out += "=";
    }

    return out;
}

// Parse "KEY=VALUE,KEY=VALUE" into a JSON object, inferring bool/number/string per value.
nlohmann::json parse_kv_config(const std::string &cfg) {
    nlohmann::json obj = nlohmann::json::object();
    if (cfg.empty())
        return obj;

    std::stringstream ss(cfg);
    std::string pair;
    while (std::getline(ss, pair, ',')) {
        const size_t eq = pair.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = pair.substr(0, eq);
        std::string value = pair.substr(eq + 1);
        if (key.empty())
            continue;

        if (value == "true") {
            obj[key] = true;
        } else if (value == "false") {
            obj[key] = false;
        } else {
            try {
                size_t consumed = 0;
                if (value.find('.') != std::string::npos) {
                    double d = std::stod(value, &consumed);
                    if (consumed == value.size()) {
                        obj[key] = d;
                        continue;
                    }
                } else {
                    long long n = std::stoll(value, &consumed);
                    if (consumed == value.size()) {
                        obj[key] = n;
                        continue;
                    }
                }
            } catch (const std::exception &) {
                // fall through to string
            }
            obj[key] = value;
        }
    }
    return obj;
}

size_t curl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *out = static_cast<std::string *>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

} // namespace

OpenAIHttpBackend::OpenAIHttpBackend(const HttpBackendParams &params)
    : model_name_(params.model_name), api_key_(params.api_key), include_metrics_(params.include_metrics) {
    if (params.server_url.empty()) {
        throw std::runtime_error("HTTP backend requires a non-empty server_url");
    }
    if (model_name_.empty()) {
        throw std::runtime_error("HTTP backend requires a non-empty model name");
    }

    // Strip trailing slash, then append the Chat Completions endpoint path.
    std::string base = params.server_url;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    chat_completions_url_ = base + "/chat/completions";

    timeout_ms_ = 30000; // default 30s
    if (!params.timeout_ms.empty()) {
        try {
            timeout_ms_ = std::stol(params.timeout_ms);
        } catch (const std::exception &e) {
            throw std::runtime_error("Invalid http-timeout value '" + params.timeout_ms + "': " + e.what());
        }
    }

    ensure_curl_global_init();

    GST_INFO("Initialized OpenAI-HTTP backend: url=%s, model=%s", chat_completions_url_.c_str(), model_name_.c_str());
}

OpenAIHttpBackend::~OpenAIHttpBackend() = default;

void OpenAIHttpBackend::set_generation_config(const std::string &cfg) {
    generation_config_ = cfg;
}

std::future<GenAIResult> OpenAIHttpBackend::submit(GenRequest req) {
    std::promise<GenAIResult> promise;
    std::future<GenAIResult> future = promise.get_future();

    // Synchronous backend: perform the request now and return an already-satisfied future.
    try {
        if (req.as_video) {
            GST_WARNING("openai-http backend does not support vision-mode=video; sending frames as images instead");
        }
        if (req.frames.empty()) {
            throw std::runtime_error("Cannot run inference with no accumulated frames");
        }

        // Encode each RGB frame tensor to a base64 JPEG data URL.
        nlohmann::json content = nlohmann::json::array();
        content.push_back({{"type", "text"}, {"text", req.prompt}});
        for (const auto &tensor : req.frames) {
            cv::Mat bgr = rgb_tensor_to_bgr_mat(tensor);
            std::vector<uchar> jpeg_bytes;
            if (!cv::imencode(".jpg", bgr, jpeg_bytes)) {
                throw std::runtime_error("Failed to JPEG-encode frame for HTTP backend");
            }
            const std::string data_url = "data:image/jpeg;base64," + base64_encode(jpeg_bytes);
            content.push_back({{"type", "image_url"}, {"image_url", {{"url", data_url}}}});
        }

        // Build the Chat Completions request body.
        nlohmann::json body = parse_kv_config(generation_config_);
        body["model"] = model_name_;
        body["messages"] = nlohmann::json::array({{{"role", "user"}, {"content", content}}});
        if (!body.contains("max_tokens")) {
            body["max_tokens"] = 100; // parity with the OpenVINO backend's default
        }
        const std::string request_body = body.dump();

        // Perform the HTTP POST via libcurl.
        CURL *curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize libcurl handle");
        }

        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string auth_header;
        if (!api_key_.empty()) {
            auth_header = "Authorization: Bearer " + api_key_;
            headers = curl_slist_append(headers, auth_header.c_str());
        }

        std::string response_body;
        curl_easy_setopt(curl, CURLOPT_URL, chat_completions_url_.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms_);

        CURLcode res = curl_easy_perform(curl);
        long http_status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(res));
        }
        if (http_status < 200 || http_status >= 300) {
            throw std::runtime_error("HTTP request failed with status " + std::to_string(http_status) + ": " +
                                     response_body);
        }

        nlohmann::json response = nlohmann::json::parse(response_body);
        if (!response.contains("choices") || response["choices"].empty()) {
            throw std::runtime_error("Server response missing 'choices': " + response_body);
        }

        const std::string text = response["choices"][0]["message"]["content"].get<std::string>();

        auto round_2dp = [](double value) { return std::round(value * 100.0) / 100.0; };
        nlohmann::ordered_json json_obj = {{"result", text}};
        if (include_metrics_ && response.contains("usage")) {
            json_obj["metrics"] = response["usage"];
        }
        if (GST_CLOCK_TIME_IS_VALID(req.timestamp)) {
            json_obj["timestamp"] = req.timestamp;
            json_obj["timestamp_seconds"] = round_2dp((double)req.timestamp / GST_SECOND);
        }

        GenAIResult result;
        result.text = text;
        result.confidence = -1.0f; // Chat Completions API doesn't return a confidence score
        result.raw_json = json_obj.dump();
        result.backend_name = describe();

        promise.set_value(std::move(result));
    } catch (...) {
        promise.set_exception(std::current_exception());
    }

    return future;
}

} // namespace genai
