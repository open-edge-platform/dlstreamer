/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Typed access to the string-keyed InferenceConfig map that the inference elements pass to the OpenVINO backend
// (device, nireq, batch size, reshape, pre-processing, input layers, OpenVINO properties).

#pragma once

#include "inference_backend/image_inference.h"

#include "inference_backend/logger.h"
#include "inference_backend/pre_proc.h"

#include "config.h"

#include <dlstreamer/element.h>

#include <openvino/openvino.hpp>
#include <openvino/runtime/intel_gpu/properties.hpp>
#include <openvino/runtime/intel_npu/properties.hpp>

#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace InferenceBackend {
namespace OpenVino {

// Maps an input-layer precision name from the config ("U8" or "FP32") to an OpenVINO element type.
inline ov::element::Type str_to_ov_type(const std::string &type_str) {
    ov::element::Type ov_type = ov::element::f32;
    if (type_str == "U8") {
        ov_type = ov::element::u8;
    } else if (type_str != "FP32") {
        throw std::runtime_error("Unsupported input_layer precision: " + type_str);
    }

    return ov_type;
}

// Read-only view over an InferenceConfig. Holds a reference, so the config must outlive the helper.
struct ConfigHelper {
    const InferenceBackend::InferenceConfig &config;
    const std::map<std::string, std::string> &base_config = config.at(KEY_BASE);
    const std::string empty_str;

    struct InputConfig {
        ov::element::Type type;
        std::string data_format; // Format of input data: image, image_info, sequence_index, etc.
    };
    using InputsConfig = std::map<std::string, InputConfig>;

    ConfigHelper(const InferenceBackend::InferenceConfig &cfg) : config(cfg) {
    }

    const std::string &device() const {
        return base_config.at(KEY_DEVICE);
    }

    int nireq() const {
        return std::stoi(base_config.at(KEY_NIREQ));
    }

    const std::string model_path() const {
        return base_config.at(KEY_MODEL);
    }

    const std::string custom_preproc_lib() const {
        return base_config.at(KEY_CUSTOM_PREPROC_LIB);
    }

    const std::string ov_extension_lib() const {
        return base_config.at(KEY_OV_EXTENSION_LIB);
    }

    int batch_size() const {
        return std::stoi(base_config.at(KEY_BATCH_SIZE));
    }

    int batch_timeout() const {
        const auto inference_config = config.find(InferenceBackend::KEY_INFERENCE);
        if (inference_config == config.end())
            return -1;
        const auto it = inference_config->second.find(ov::auto_batch_timeout.name());
        if (it == inference_config->second.end())
            return -1;
        return std::stoi(it->second);
    }

    const std::string &image_format() const {
        return base_get_or_empty(KEY_IMAGE_FORMAT);
    }

    const std::string &model_format() const {
        static const std::string bgr_str = std::string("BGR");
        const std::string &format = base_get_or_empty(KEY_MODEL_FORMAT);
        if (format == empty_str)
            return bgr_str;
        return format;
    }

    static std::vector<float> string_to_floats(const std::string &str) {
        if (str.empty()) {
            return {};
        }
        try {
            std::istringstream ss(str);
            std::vector<float> result;
            float val = 0.0f;
            while (ss >> val) {
                result.push_back(val);
            }
            return result;
        } catch (const std::invalid_argument &e) {
            // Handle the exception if the conversion fails
            GVA_ERROR("Invalid argument: %s", e.what());
            std::throw_with_nested(std::runtime_error("Pre-processing was failed."));
        } catch (const std::out_of_range &e) {
            // Handle the exception if the value is out of range for the float type
            GVA_ERROR("Out of range: %s", e.what());
            std::throw_with_nested(std::runtime_error("Pre-processing was failed."));
        }
    }

    std::vector<float> pixel_value_mean() const {
        return string_to_floats(base_get_or_empty(KEY_PIXEL_VALUE_MEAN));
    }

    std::vector<float> pixel_value_scale() const {
        return string_to_floats(base_get_or_empty(KEY_PIXEL_VALUE_SCALE));
    }

    bool need_reshape() const {
        const auto it = base_config.find(KEY_RESHAPE);
        if (it == base_config.cend())
            return false;
        return std::stoi(it->second);
    }

    bool reshape_static() const {
        const auto it = base_config.find(KEY_RESHAPE_STATIC);
        if (it == base_config.cend())
            return false;
        return std::stoi(it->second);
    }

    const std::string &base_get_or_empty(const std::string &key) const {
        const auto it = base_config.find(key);
        if (it == base_config.cend())
            return empty_str;
        return it->second;
    }

    size_t base_get_or(const std::string &key, size_t or_value) const {
        const auto it = base_config.find(key);
        if (it == base_config.cend())
            return or_value;
        return std::stoul(it->second);
    }

    std::pair<size_t, size_t> reshape_size() const {
        return {base_get_or(KEY_RESHAPE_WIDTH, 0), base_get_or(KEY_RESHAPE_HEIGHT, 0)};
    }

    std::pair<size_t, size_t> image_size() const {
        return {base_get_or("img-width", 0), base_get_or("img-height", 0)};
    }

    std::pair<size_t, size_t> frame_size() const {
        return {base_get_or("frame-width", 0), base_get_or("frame-height", 0)};
    }

    InferenceBackend::ImagePreprocessorType pp_type() const {
        const std::string &preprocessor_type_str = base_config.count(InferenceBackend::KEY_PRE_PROCESSOR_TYPE)
                                                       ? base_config.at(InferenceBackend::KEY_PRE_PROCESSOR_TYPE)
                                                       : "";
        return static_cast<InferenceBackend::ImagePreprocessorType>(std::stoi(preprocessor_type_str));
    }

    // Converts the string-valued "inference" section to typed OpenVINO properties; unknown keys are rejected.
    static ov::AnyMap params_map_to_openvino_map(const std::map<std::string, std::string> &params) {
        ov::AnyMap m;
        for (auto &item : params) {
            if (item.first == ov::num_streams.name()) {
                m.emplace(item.first, ov::streams::Num(stoi(item.second)));
            } else if (item.first == ov::log::level.name() || item.first == ov::cache_mode.name() ||
                       item.first == ov::hint::enable_cpu_pinning.name() || item.first == ov::enable_profiling.name() ||
                       item.first == ov::hint::model_priority.name() ||
                       item.first == ov::hint::performance_mode.name() ||
                       item.first == ov::hint::scheduling_core_type.name() ||
                       item.first == ov::hint::execution_mode.name() ||
                       item.first == ov::hint::enable_cpu_pinning.name() ||
                       item.first == ov::hint::enable_hyper_threading.name() ||
                       item.first == ov::hint::allow_auto_batching.name() ||
                       item.first == ov::hint::inference_precision.name() ||
                       item.first == ov::intel_gpu::enable_loop_unrolling.name() ||
                       item.first == ov::intel_gpu::disable_winograd_convolution.name() ||
                       item.first == ov::intel_gpu::hint::queue_throttle.name() ||
                       item.first == ov::intel_gpu::hint::queue_priority.name() ||
                       item.first == ov::intel_gpu::hint::host_task_priority.name() ||
                       item.first == ov::intel_gpu::hint::enable_sdpa_optimization.name() ||
                       item.first == ov::intel_npu::turbo.name() || item.first == ov::intel_npu::tiles.name() ||
                       item.first == ov::intel_npu::compilation_mode_params.name()) {
                m.emplace(item.first, item.second);
            } else if (item.first == ov::optimal_batch_size.name() || item.first == ov::max_batch_size.name() ||
                       item.first == ov::auto_batch_timeout.name() || item.first == ov::inference_num_threads.name() ||
                       item.first == ov::compilation_num_threads.name() ||
                       item.first == ov::hint::num_requests.name()) {
                m.emplace(item.first, stoi(item.second));
            } else {
                throw std::runtime_error("Unsupported inference param " + item.first);
            }
        }
        return m;
    }

    ov::AnyMap inference_cfg() const {
        return params_map_to_openvino_map(config.at(InferenceBackend::KEY_INFERENCE));
    }

    InputsConfig inputs_cfg() const {
        InputsConfig res;
        const auto &input_p = config.at(InferenceBackend::KEY_INPUT_LAYER_PRECISION);
        for (auto &item : input_p)
            res[item.first].type = str_to_ov_type(item.second);

        const auto &input_f = config.at(InferenceBackend::KEY_FORMAT);
        for (auto &item : input_f)
            res[item.first].data_format = item.second;
        return res;
    }

    const std::string &logger_name() const {
        return base_get_or_empty(dlstreamer::param::logger_name);
    }
};

// Logs the per-input precision and data format taken from the config.
inline void print_inputs_config(const ConfigHelper::InputsConfig &inputs_cfg) {
    if (inputs_cfg.empty()) {
        GVA_INFO("Inputs configuration is not provided");
        return;
    }

    GVA_INFO("Provided inputs configuration:");
    for (const auto &item : inputs_cfg) {
        std::string_view fmt{"<null>"};
        if (!item.second.data_format.empty())
            fmt = item.second.data_format;
        GVA_INFO("  %s: %s, %s", item.first.c_str(), item.second.type.get_type_name().c_str(),
                 std::string(fmt).c_str());
    }
}

} // namespace OpenVino
} // namespace InferenceBackend
