/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "dlstreamer_logger.h"

#include <openvino/runtime/properties.hpp>

#include <dlstreamer/openvino/context.h>
#ifdef _WIN32
#include <dlstreamer/d3d11/context.h>
#endif
// For logger_name
#include <dlstreamer/element.h>

#include <spdlog/fmt/bundled/ranges.h>

#include "openvino_image_inference.h"

#include "config.h"
#include "inference_backend/logger.h"
#include "inference_backend/pre_proc.h"

#include "openvino_blob_wrapper.h"
#include "safe_arithmetic.hpp"
#include "utils.h"

#ifndef _WIN32
#ifdef ENABLE_VAAPI
#include <dlstreamer/vaapi/context.h>
#ifdef ENABLE_GPU_TILE_AFFINITY
#include "vaapi_utils.h"
#endif
#endif
#endif

#include <functional>
#include <iterator>
#include <regex>
#include <stdio.h>
#include <thread>

using namespace InferenceBackend;

template <>
struct fmt::formatter<InferenceBackend::ImagePreprocessorType> : formatter<string_view> {
    auto format(InferenceBackend::ImagePreprocessorType t, format_context &ctx) const {
        using InferenceBackend::ImagePreprocessorType;
        string_view name = "<unknown>";
        switch (t) {
        case ImagePreprocessorType::AUTO:
            name = "auto";
            break;
        case ImagePreprocessorType::IE:
            name = "IE";
            break;
        case ImagePreprocessorType::OPENCV:
            name = "OpenCV";
            break;
        case ImagePreprocessorType::VAAPI_SYSTEM:
            name = "VAAPI System Memory";
            break;
        case ImagePreprocessorType::VAAPI_SURFACE_SHARING:
            name = "VAAPI Surface Sharing";
            break;
        case ImagePreprocessorType::D3D11:
            name = "D3D11 System Memory";
            break;
        case ImagePreprocessorType::D3D11_SURFACE_SHARING:
            name = "D3D11 Surface Sharing";
            break;
        }
        return formatter<string_view>::format(name, ctx);
    }
};

template <>
struct fmt::formatter<std::exception_ptr> {
    // Max level of nesting printed (format parameter?)
    static constexpr size_t max_level = 5;
    mutable size_t level = 0;

    // Fix for an AFL++ compilation issue
    using return_type = decltype(std::declval<format_context>().out());

    constexpr auto parse(format_parse_context &ctx) {
        return ctx.begin();
    }

    size_t current_indent() const {
        return level * 4;
    }

    template <typename T>
    return_type format_nested(const T &ex, format_context &ctx) const {
        try {
            std::rethrow_if_nested(ex);
        } catch (...) {
            if (level > max_level)
                return fmt::format_to(ctx.out(), "\n{:.^{}}<...>", "", current_indent());
            ctx.advance_to(fmt::format_to(ctx.out(), "\n{:.^{}}", "", current_indent()));
            return format(std::current_exception(), ctx);
        }
        return ctx.out();
    }

    return_type format(const std::exception_ptr &ex_ptr, format_context &ctx) const {
        if (!ex_ptr)
            return fmt::format_to(ctx.out(), "<exception is nullptr>");

        try {
            std::rethrow_exception(ex_ptr);
        } catch (const std::exception &e) {
            ctx.advance_to(fmt::format_to(ctx.out(), "{}", e.what()));
            level++;
            return format_nested(e, ctx);
        } catch (const std::string &e) {
            return fmt::format_to(ctx.out(), "{}", e);
        } catch (...) {
            return fmt::format_to(ctx.out(), "<unknown exception type>");
        }

        return ctx.out();
    }
};

template <>
struct fmt::formatter<ov::AnyMap::value_type> {
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.begin();
    }

    auto format(const ov::AnyMap::value_type &pair, format_context &ctx) const {
        return fmt::format_to(ctx.out(), "{}: {}", pair.first, pair.second.as<std::string>());
    }
};

namespace {

std::vector<std::string> split(const std::string &s, const std::string &delimiters) {
    std::regex re("[" + delimiters + "]+");
    std::sregex_token_iterator first{s.begin(), s.end(), re, -1}, last;
    return {first, last};
}

std::vector<std::string> extractNumbers(const std::string &s) {
    // Regular expression to match numbers, including negative and floating-point numbers
    std::regex re(R"([-+]?\d*\.?\d+)");
    std::sregex_iterator begin(s.begin(), s.end(), re);
    std::sregex_iterator end;

    std::vector<std::string> numbers;
    for (std::sregex_iterator i = begin; i != end; ++i) {
        numbers.push_back(i->str());
    }

    return numbers;
}

const InputImageLayerDesc::Ptr
getImagePreProcInfo(const std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> &input_preprocessors) {
    const auto image_it = input_preprocessors.find("image");
    if (image_it != input_preprocessors.cend()) {
        const auto description = image_it->second;
        if (description) {
            return description->input_image_preroc_params;
        }
    }

    return nullptr;
}

void print_input_and_outputs_info(const ov::Model &network) {
    GVA_INFO("model name: %s", network.get_friendly_name().c_str());

    size_t index = 0;
    const std::vector<ov::Output<const ov::Node>> &inputs = network.inputs();
    for (const ov::Output<const ov::Node> &input : inputs) {
        GVA_INFO("    input[%zu]", index++);

        if (!input.get_names().empty()) {
            std::string input_names = fmt::format("        input names: {}", fmt::join(input.get_names(), " "));
            GVA_INFO("%s", input_names.c_str());
        } else
            GVA_INFO("        input names: <NONE>");

        const ov::element::Type type = input.get_element_type();
        GVA_INFO("        input type: %s", type.get_type_name().c_str());

        const auto &partial_shape = input.get_partial_shape();
        const auto &shape = partial_shape.is_dynamic() ? partial_shape.get_min_shape() : partial_shape.get_shape();
        GVA_INFO("        input shape: %s", shape.to_string().c_str());
    }

    index = 0;
    const std::vector<ov::Output<const ov::Node>> &outputs = network.outputs();
    for (const ov::Output<const ov::Node> &output : outputs) {
        GVA_INFO("    output[%zu]", index++);

        if (!output.get_names().empty()) {
            std::string output_names = fmt::format("        output names: {}", fmt::join(output.get_names(), " "));
            GVA_INFO("%s", output_names.c_str());
        } else
            GVA_INFO("        output names: <NONE>");

        const ov::element::Type type = output.get_element_type();
        GVA_INFO("        output type: %s", type.get_type_name().c_str());

        const auto &partial_shape = output.get_partial_shape();
        const auto &shape = partial_shape.is_dynamic() ? partial_shape.get_min_shape() : partial_shape.get_shape();
        GVA_INFO("        output shape: %s", shape.to_string().c_str());
    }
}

ov::element::Type str_to_ov_type(const std::string &type_str) {
    ov::element::Type ov_type = ov::element::f32;
    if (type_str == "U8") {
        ov_type = ov::element::u8;
    } else if (type_str != "FP32") {
        throw std::runtime_error("Unsupported input_layer precision: " + type_str);
    }

    return ov_type;
}

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
    // NO COPY
    ConfigHelper(const ConfigHelper &) = delete;
    ConfigHelper &operator=(const ConfigHelper &) = delete;

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
                       item.first == ov::intel_npu::turbo.name()) {
                m.emplace(item.first, item.second);
            } else if (item.first == ov::optimal_batch_size.name() || item.first == ov::max_batch_size.name() ||
                       item.first == ov::auto_batch_timeout.name() || item.first == ov::inference_num_threads.name() ||
                       item.first == ov::compilation_num_threads.name() ||
                       item.first == ov::hint::num_requests.name() ||
                       item.first == ov::intel_npu::compilation_mode_params.name()) {
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

} // namespace

void print_inputs_config(const ConfigHelper::InputsConfig &inputs_cfg) {
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

class OpenVinoNewApiImpl {
    friend class OpenVINOImageInference;

    static void log_api_message() {
        try {
            const std::string_view ov_build{ov::get_openvino_version().buildNumber};
            std::string box = fmt::format("\n"
                                          "┌{0:─^{3}}┐\n"
                                          "│{1: ^{3}}│\n"
                                          "│{2: ^{3}}│\n"
                                          "└{0:─^{3}}┘\n",
                                          "", ".:: OpenVINO™ via 2.0 API ::.", ov_build, ov_build.size() + 2);
            GVA_DEBUG("%s", box.c_str());
        } catch (...) {
            GVA_ERROR("Unknown exception in log_api_message");
        }
    };

  public:
#ifndef ENABLE_D3D_NPU_COLOR_CONV
    OpenVinoNewApiImpl(const ConfigHelper &config, dlstreamer::ContextPtr context,
                       ImageInference::CallbackFunc callback, ImageInference::ErrorHandlingFunc error_handler,
                       MemoryType memory_type)
        : _app_context(std::move(context)), _memory_type(memory_type), _callback(callback),
          _error_handler(error_handler) {
#else
    OpenVinoNewApiImpl(const ConfigHelper &config, dlstreamer::ContextPtr context,
                       ImageInference::CallbackFunc callback, ImageInference::ErrorHandlingFunc error_handler,
                       MemoryType memory_type, InferenceBackend::ImagePreprocessorType pp_type)
        : _app_context(std::move(context)), _memory_type(memory_type), _callback(callback), _pp_type(pp_type),
          _error_handler(error_handler) {
#endif
        log_api_message();

        _device = config.device();
        _nireq = config.nireq();

        auto ov_extension_lib = config.ov_extension_lib();
        if (!ov_extension_lib.empty()) {
            core().add_extension(ov_extension_lib);
        }

        // read model & configure model
        _model = core().read_model(config.model_path());

        {
            size_t bs;
            int fmt, mt;
            get_model_image_input_info(_origin_model_in_w, _origin_model_in_h, bs, fmt, mt);
        }

        configure_model(config);
        create_remote_context();

        // Load nn to device
        load_network(config);

        if (!_nireq)
            _nireq = _compiled_model.get_property(ov::optimal_number_of_infer_requests);
        GVA_DEBUG("Num of inference req: %d", _nireq);
    }

    ~OpenVinoNewApiImpl() {
        log_api_message();
    }

    auto get_model_inputs_info() const {
        std::map<std::string, std::vector<size_t>> res;
        for (auto node : _model->get_parameters()) {
            auto shape = node->is_dynamic() ? node->get_input_partial_shape(0).get_min_shape() : node->get_shape();
            res.emplace(node->get_friendly_name(), std::move(shape));
        }

        return res;
    }

    auto get_model_outputs_info() const {
        std::map<std::string, std::vector<size_t>> res;
        for (auto &node : _model->outputs()) {
            auto shape = node.get_node()->is_dynamic() ? node.get_partial_shape().get_min_shape() : node.get_shape();
            auto name = node.get_names().size() > 0 ? node.get_any_name() : std::string("output");
            res.emplace(name, std::move(shape));
        }
        return res;
    }

    // convert ov::Any to GstStructure
    auto get_model_info_postproc() const {
        std::map<std::string, GstStructure *> res;
        std::string layer_name("ANY");
        GstStructure *s = nullptr;
        ov::AnyMap modelConfig;

        if (_model->has_rt_info({"model_info"})) {
            modelConfig = _model->get_rt_info<ov::AnyMap>("model_info");
            s = gst_structure_new_empty(layer_name.data());
        }

        // the parameter parsing loop may use locale-dependent floating point conversion
        // save current locale and restore after the loop
        std::string oldlocale = std::setlocale(LC_ALL, nullptr);
        std::setlocale(LC_ALL, "C");

        for (auto &element : modelConfig) {
            if (element.first.find("model_type") != std::string::npos) {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_STRING);
                g_value_set_string(&gvalue, element.second.as<std::string>().c_str());
                gst_structure_set_value(s, "converter", &gvalue);
                GST_INFO("[get_model_info_postproc] model_type: %s", element.second.as<std::string>().c_str());
                GST_INFO("[get_model_info_postproc] converter: %s", g_value_get_string(&gvalue));
                g_value_unset(&gvalue);
            }
            if ((element.first.find("multilabel") != std::string::npos) &&
                (element.second.as<std::string>().find("True") != std::string::npos)) {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_STRING);
                const gchar *oldvalue = gst_structure_get_string(s, "method");
                if ((oldvalue != nullptr) && (strcmp(oldvalue, "softmax") == 0))
                    g_value_set_string(&gvalue, "softmax_multi");
                else
                    g_value_set_string(&gvalue, "multi");
                gst_structure_set_value(s, "method", &gvalue);
                GST_INFO("[get_model_info_postproc] multilabel: %s", element.second.as<std::string>().c_str());
                GST_INFO("[get_model_info_postproc] method: %s", g_value_get_string(&gvalue));
                g_value_unset(&gvalue);
            }
            if ((element.first.find("output_raw_scores") != std::string::npos) &&
                (element.second.as<std::string>().find("True") != std::string::npos)) {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_STRING);
                const gchar *oldvalue = gst_structure_get_string(s, "method");
                if ((oldvalue != nullptr) && (strcmp(oldvalue, "multi") == 0))
                    g_value_set_string(&gvalue, "softmax_multi");
                else
                    g_value_set_string(&gvalue, "softmax");
                gst_structure_set_value(s, "method", &gvalue);
                GST_INFO("[get_model_info_postproc] output_raw_scores: %s", element.second.as<std::string>().c_str());
                GST_INFO("[get_model_info_postproc] method: %s", g_value_get_string(&gvalue));
                g_value_unset(&gvalue);
            }
            if (element.first.find("confidence_threshold") != std::string::npos) {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_DOUBLE);
                g_value_set_double(&gvalue, element.second.as<double>());
                gst_structure_set_value(s, "confidence_threshold", &gvalue);
                GST_INFO("[get_model_info_postproc] confidence_threshold: %f", element.second.as<double>());
                g_value_unset(&gvalue);
            }
            if (element.first.find("iou_threshold") != std::string::npos) {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_DOUBLE);
                g_value_set_double(&gvalue, element.second.as<double>());
                gst_structure_set_value(s, "iou_threshold", &gvalue);
                GST_INFO("[get_model_info_postproc] iou_threshold: %f", element.second.as<double>());
                g_value_unset(&gvalue);
            }
            if (element.first.find("image_threshold") != std::string::npos) {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_DOUBLE);
                g_value_set_double(&gvalue, element.second.as<double>());
                gst_structure_set_value(s, "image_threshold", &gvalue);
                GST_INFO("[get_model_info_postproc] image_threshold: %f", element.second.as<double>());
                g_value_unset(&gvalue);
            }
            if (element.first.find("pixel_threshold") != std::string::npos) {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_DOUBLE);
                g_value_set_double(&gvalue, element.second.as<double>());
                gst_structure_set_value(s, "pixel_threshold", &gvalue);
                GST_INFO("[get_model_info_postproc] pixel_threshold: %f", element.second.as<double>());
                g_value_unset(&gvalue);
            }
            if (element.first.find("normalization_scale") != std::string::npos) {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_DOUBLE);
                g_value_set_double(&gvalue, element.second.as<double>());
                gst_structure_set_value(s, "normalization_scale", &gvalue);
                GST_INFO("[get_model_info_postproc] normalization_scale: %f", element.second.as<double>());
                g_value_unset(&gvalue);
            }
            if (element.first.find("task") != std::string::npos) {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_STRING);
                g_value_set_string(&gvalue, element.second.as<std::string>().c_str());
                gst_structure_set_value(s, "anomaly_task", &gvalue);
                GST_INFO("[get_model_info_postproc] anomaly_task: %s", element.second.as<std::string>().c_str());
                g_value_unset(&gvalue);
            }
            if (element.first.find("labels") != std::string::npos) {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, GST_TYPE_ARRAY);
                std::string labels_string = element.second.as<std::string>();
                std::vector<std::string> labels = split(labels_string, ",; ");
                for (auto &el : labels) {
                    GValue label = G_VALUE_INIT;
                    g_value_init(&label, G_TYPE_STRING);
                    g_value_set_string(&label, el.c_str());
                    gst_value_array_append_value(&gvalue, &label);
                    g_value_unset(&label);
                    GST_INFO("[get_model_info_postproc] label: %s", el.c_str());
                }
                gst_structure_set_value(s, "labels", &gvalue);
                g_value_unset(&gvalue);
            }
        }

        // restore system locale
        std::setlocale(LC_ALL, oldlocale.c_str());

        if (s != nullptr)
            res[layer_name] = s;

        return res;
    }

    // convert ov::Any to GstStructure
    static std::map<std::string, GstStructure *>
    get_model_info_preproc(const std::string model_file, const gchar *pre_proc_config, const gchar *ov_extension_lib) {
        std::map<std::string, GstStructure *> res;
        std::string layer_name("ANY");
        GstStructure *s = nullptr;
        ov::AnyMap modelConfig;

        if (ov_extension_lib && ov_extension_lib[0] != '\0') {
            core().add_extension(ov_extension_lib);
        }

        std::shared_ptr<ov::Model> _model;
        _model = core().read_model(model_file);

        // Warn if model quantization runtime does not match current runtime
        if (_model->has_rt_info({"nncf"})) {
            const ov::AnyMap nncfConfig = _model->get_rt_info<const ov::AnyMap>("nncf");
            const std::string modelVersion = _model->get_rt_info<const std::string>("Runtime_version");
            const std::string runtimeVersion = ov::get_openvino_version().buildNumber;

            if (nncfConfig.count("quantization") && (modelVersion != runtimeVersion))
                g_warning("Model quantization runtime (%s) does not match current runtime (%s). Results may be "
                          "inaccurate. Please re-quantize the model with the current runtime version.",
                          modelVersion.c_str(), runtimeVersion.c_str());
        }

        if (_model->has_rt_info({"model_info"})) {
            modelConfig = _model->get_rt_info<ov::AnyMap>("model_info");
            s = gst_structure_new_empty(layer_name.data());
        }

        // override model config with command line pre-processing parameters if provided
        auto pre_proc_params = Utils::stringToMap(pre_proc_config);
        for (auto &item : pre_proc_params) {
            if (modelConfig.find(item.first) != modelConfig.end()) {
                modelConfig[item.first] = item.second;
            }
        }

        // the parameter parsing loop may use locale-dependent floating point conversion
        // save current locale and restore after the loop
        std::string oldlocale = std::setlocale(LC_ALL, nullptr);
        std::setlocale(LC_ALL, "C");

        for (auto &element : modelConfig) {
            if (element.first == "scale_values") {
                std::vector<std::string> values = extractNumbers(element.second.as<std::string>());
                if (values.size() == 1) {
                    GValue gvalue = G_VALUE_INIT;
                    g_value_init(&gvalue, G_TYPE_DOUBLE);
                    g_value_set_double(&gvalue, element.second.as<double>());
                    gst_structure_set_value(s, "scale", &gvalue);
                    GST_INFO("[get_model_info_preproc] scale: %f", element.second.as<double>());
                    g_value_unset(&gvalue);
                } else if (values.size() == 3) {

                    std::vector<double> scale_values;
                    // If there are three values, use them directly
                    for (const std::string &valueStr : values) {
                        scale_values.push_back(std::stod(valueStr));
                    }
                    // Create a GST_TYPE_ARRAY to hold the scale values
                    GValue gvalue = G_VALUE_INIT;
                    g_value_init(&gvalue, GST_TYPE_ARRAY);
                    for (double scale_value : scale_values) {
                        GValue item = G_VALUE_INIT;
                        g_value_init(&item, G_TYPE_DOUBLE);
                        g_value_set_double(&item, scale_value);
                        gst_value_array_append_value(&gvalue, &item);
                        GST_INFO("[get_model_info_preproc] scale_values: %f", scale_value);
                        g_value_unset(&item);
                    }

                    // Set the array in the GstStructure
                    gst_structure_set_value(s, "std", &gvalue);
                    g_value_unset(&gvalue);
                } else {
                    throw std::runtime_error("Invalid number of scale values. Expected 1 or 3 values.");
                }
            }
            if (element.first == "mean_values") {
                std::vector<std::string> values = extractNumbers(element.second.as<std::string>());
                std::vector<double> scale_values;

                if (values.size() == 3) {
                    // If there are three values, use them directly
                    for (const std::string &valueStr : values) {
                        scale_values.push_back(std::stod(valueStr));
                    }
                } else {
                    throw std::runtime_error("Invalid number of mean values. Expected 3 values.");
                }

                // Create a GST_TYPE_ARRAY to hold the scale values
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, GST_TYPE_ARRAY);
                for (double scale_value : scale_values) {
                    GValue item = G_VALUE_INIT;
                    g_value_init(&item, G_TYPE_DOUBLE);
                    g_value_set_double(&item, scale_value);
                    gst_value_array_append_value(&gvalue, &item);
                    g_value_unset(&item);
                }

                // Set the array in the GstStructure
                gst_structure_set_value(s, "mean", &gvalue);
                GST_INFO("[get_model_info_preproc] mean: %s", g_value_get_string(&gvalue));
                g_value_unset(&gvalue);
            }
            if (element.first == "resize_type") {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_STRING);

                if (element.second.as<std::string>() == "crop") {
                    g_value_set_string(&gvalue, "central-resize");
                    gst_structure_set_value(s, "crop", &gvalue);
                }
                if (element.second.as<std::string>() == "fit_to_window_letterbox") {
                    g_value_set_string(&gvalue, "aspect-ratio");
                    gst_structure_set_value(s, "resize", &gvalue);
                }
                if (element.second.as<std::string>() == "fit_to_window") {
                    g_value_set_string(&gvalue, "aspect-ratio-pad");
                    gst_structure_set_value(s, "resize", &gvalue);
                }
                if (element.second.as<std::string>() == "standard") {
                    g_value_set_string(&gvalue, "no-aspect-ratio");
                    gst_structure_set_value(s, "resize", &gvalue);
                }
                GST_INFO("[get_model_info_preproc] resize_type: %s", element.second.as<std::string>().c_str());
                GST_INFO("[get_model_info_preproc] resize: %s", g_value_get_string(&gvalue));
                g_value_unset(&gvalue);
            }
            if (element.first == "color_space") {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_STRING);
                g_value_set_string(&gvalue, element.second.as<std::string>().c_str());
                gst_structure_set_value(s, "color_space", &gvalue);
                GST_INFO("[get_model_info_preproc] reverse_input_channels: %s",
                         element.second.as<std::string>().c_str());
                GST_INFO("[get_model_info_preproc] color_space: %s", g_value_get_string(&gvalue));
                g_value_unset(&gvalue);
            }
            if (element.first == "reverse_input_channels") {
                GValue gvalue = G_VALUE_INIT;
                g_value_init(&gvalue, G_TYPE_INT);
                g_value_set_int(&gvalue, gint(false));

                std::transform(element.second.as<std::string>().begin(), element.second.as<std::string>().end(),
                               element.second.as<std::string>().begin(), ::tolower);

                if (element.second.as<std::string>() == "yes" || element.second.as<std::string>() == "true")
                    g_value_set_int(&gvalue, gint(true));

                gst_structure_set_value(s, "reverse_input_channels", &gvalue);
                GST_INFO("[get_model_info_preproc] reverse_input_channels: %s",
                         element.second.as<std::string>().c_str());
                g_value_unset(&gvalue);
            }
        }

        // restore system locale
        std::setlocale(LC_ALL, oldlocale.c_str());

        if (s != nullptr)
            res[layer_name] = s;

        return res;
    }

    void get_model_image_input_info(size_t &width, size_t &height, size_t &batch_size, int &format, int &memory_type) {
        width = height = batch_size = 0;

        for (auto input : _model->inputs()) {
            // FIXME: strange dependency on input image name
            if (!_image_input_name.empty() && !input.get_names().count(_image_input_name))
                continue;

            const auto &partial_shape = input.get_partial_shape();
            const auto &shape = partial_shape.is_dynamic() ? partial_shape.get_min_shape() : partial_shape.get_shape();
            const auto layout = get_ov_node_layout(input, true);
            GVA_DEBUG("get_model_image_input_info(): input: %s, shape: %s, partial shape: %s, layout: %s",
                      input.get_any_name().c_str(), shape.to_string().c_str(), partial_shape.to_string().c_str(),
                      layout.to_string().c_str());

            if (ov::layout::has_batch(layout)) {
                const std::int64_t batch_idx = ov::layout::batch_idx(layout);
                batch_size = shape[batch_idx];
            }

            if (ov::layout::has_width(layout)) {
                const std::int64_t width_idx = ov::layout::width_idx(layout);
                width = shape[width_idx];
            }

            if (ov::layout::has_height(layout)) {
                const std::int64_t height_idx = ov::layout::height_idx(layout);
                height = shape[height_idx];
            }

            break;
        }

        if (width == 0 && height == 0) {
            width = _origin_model_in_w;
            height = _origin_model_in_h;
            GVA_DEBUG("get_model_image_input_info(): using wa, w=%zu, h=%zu", width, height);
        }

        if (batch_size == 0) {
            batch_size = _batch_size;
        }

        switch (_memory_type) {
        case MemoryType::SYSTEM:
#ifndef ENABLE_D3D_NPU_COLOR_CONV
            if (_model_format == "BGR")
                format = FourCC::FOURCC_BGRP;
            else
                format = FourCC::FOURCC_RGBP;
            break;
#else
            if (_pp_type == InferenceBackend::ImagePreprocessorType::D3D11) {
                if (_model_format == "BGR")
                    format = FourCC::FOURCC_BGRX;
                else
                    format = FourCC::FOURCC_RGBX;
                break;
            } else {
                if (_model_format == "BGR")
                    format = FourCC::FOURCC_BGRP;
                else
                    format = FourCC::FOURCC_RGBP;
                break;
            }
#endif
        case MemoryType::VAAPI:
        case MemoryType::D3D11:
            format = FourCC::FOURCC_NV12;
            break;
        default:
            throw std::runtime_error("Unsupported memory type");
        }
        memory_type = static_cast<int>(_memory_type);

        if (_was_resize) {
            // Set original model width and height values if the resizing was done.
            // Applicable for ImagePreprocessorType::IE only.
            width = _origin_model_in_w;
            height = _origin_model_in_h;
        }
    }

    static bool image_has_roi(const Image &image) {
        const auto r = image.rect;
        return r.x || r.y || ((r.width > 0) && (r.width != image.width)) ||
               ((r.height > 0) && (r.height != image.height));
    }

    std::vector<ov::Tensor> image_to_tensors(const Image &image) {
        switch (image.format) {
        case FourCC::FOURCC_RGBP:
        case FourCC::FOURCC_BGRP:
            return {image_rgbp_to_tensor(image)};

        case FourCC::FOURCC_BGRA:
        case FourCC::FOURCC_BGRX:
        case FourCC::FOURCC_RGBA:
        case FourCC::FOURCC_RGBX:
        case FourCC::FOURCC_BGR:
            return {image_bgrx_to_tensor(image)};

        case FourCC::FOURCC_NV12:
            if (image.type != MemoryType::VAAPI)
                return image_nv12_to_tensor(image);
            return image_nv12_surface_to_tensor(image);

        case FourCC::FOURCC_I420:
            return image_i420_to_tensor(image);

        default:
            throw std::logic_error("Unsupported image type");
        }
    }

    ov::Tensor image_rgbp_to_tensor(const Image &image) {
        // Input image has 3 planes, separately for RBG (or BGR)
        // This function converts 3 planes to ov::Tensor with NCHW layout
        // planes must be equally spaced in memory, and have same stride
        assert(image.planes[0] && image.planes[1] && image.planes[2]);
        assert((image.planes[1] - image.planes[0]) == (image.planes[2] - image.planes[1]));
        assert((image.stride[0] == image.stride[1]) && (image.stride[1] == image.stride[2]));

        auto channels_num = get_channels_num(image.format);
        const ov::Shape shape{1, channels_num, size_t(image.height), size_t(image.width)};
        unsigned long plane_stride = image.planes[1] - image.planes[0];
        ov::Strides stride{channels_num * plane_stride, plane_stride, image.stride[0], 1};
        ov::Tensor tensor{ov::element::u8, shape, image.planes[0], stride};

        // ROI
        if (image_has_roi(image)) {
            const auto &r = image.rect;
            const ov::Coordinate begin{0, 0, r.y, r.x};
            const ov::Coordinate end{shape[0], shape[1], r.y + r.height, r.x + r.width};
            tensor = ov::Tensor(tensor, begin, end);
        }

        // Allocate new tensor in host memory and COPY data if the original tensor is not contigous
        // - NPU device plugin requires contigous tensors (explicit assert)
        // - GPU plugin fails in certain cases with non-contigous tensors
        if (!tensor.is_continuous()) {
            ov::Tensor sparse_tensor(tensor);
            tensor = ov::Tensor(ov::element::u8, sparse_tensor.get_shape());
            sparse_tensor.copy_to(tensor);
        }

        return tensor;
    }

    ov::Tensor image_bgrx_to_tensor(const Image &image) {
        // NHWC
        auto channels_num = get_channels_num(image.format);
        ov::Shape shape{1, size_t(image.height), size_t(image.width), channels_num};
        ov::Strides stride{size_t(image.height) * image.stride[0], image.stride[0], channels_num, 1};
        ov::Tensor tensor{ov::element::u8, shape, image.planes[0], stride};
        if (image_has_roi(image)) {
            const auto &r = image.rect;
            const ov::Coordinate begin{0, r.y, r.x, 0};
            const ov::Coordinate end{shape[0], r.y + r.height, r.x + r.width, shape[3]};
            tensor = ov::Tensor(tensor, begin, end);
        }
        return tensor;
    }

    std::vector<ov::Tensor> image_nv12_to_tensor(const Image &image) {
        // NHWC, 1 for channes as two separate tensors are used
        size_t y_channels_num = 1;
        ov::Shape y_shape{1, size_t(image.height), size_t(image.width), y_channels_num};
        ov::Strides y_stride{size_t(image.height) * image.stride[0], image.stride[0], y_channels_num, 1};
        ov::Tensor y_tensor{ov::element::u8, y_shape, image.planes[0] + image.offsets[0], y_stride};

        // U and V planes are half of Y plane. 2 channels
        size_t uv_channels_num = 2;
        ov::Shape uv_shape{1, size_t(image.height) / 2, size_t(image.width) / 2, uv_channels_num};
        ov::Strides uv_stride{size_t(image.height) / 2 * image.stride[1], image.stride[1], uv_channels_num, 1};
        ov::Tensor uv_tensor{ov::element::u8, uv_shape, image.planes[1] + image.offsets[1], uv_stride};

        // ROI
        if (image_has_roi(image)) {
            const auto &r = image.rect;
            ov::Coordinate y_begin{0, r.y, r.x, 0};
            ov::Coordinate y_end{y_shape[0], r.y + r.height, r.x + r.width, 1};
            y_tensor = ov::Tensor(y_tensor, y_begin, y_end);

            ov::Coordinate uv_begin{0, r.y / 2, r.x / 2, 0};
            ov::Coordinate uv_end{uv_shape[0], (r.y + r.height) / 2, (r.x + r.width) / 2, 2};
            uv_tensor = ov::Tensor(uv_tensor, uv_begin, uv_end);
        }

        return {y_tensor, uv_tensor};
    }

    std::vector<ov::Tensor> image_nv12_surface_to_tensor(const Image &image) {
        auto rmt_ctx = _openvino_context->remote_context();
        auto va_surface = image.va_surface_id;
        auto width = image.width;
        auto height = image.height;

        ov::AnyMap tensor_params = {{ov::intel_gpu::shared_mem_type.name(), "VA_SURFACE"},
                                    {ov::intel_gpu::dev_object_handle.name(), va_surface},
                                    {ov::intel_gpu::va_plane.name(), uint32_t(0)}};
        auto y_tensor = rmt_ctx.create_tensor(ov::element::u8, {1, height, width, 1}, tensor_params);
        tensor_params[ov::intel_gpu::va_plane.name()] = uint32_t(1);
        auto uv_tensor = rmt_ctx.create_tensor(ov::element::u8, {1, height / 2, width / 2, 2}, tensor_params);

        return {y_tensor, uv_tensor};
    }

    std::vector<ov::Tensor> image_i420_to_tensor(const Image &image) const {
        assert(image.planes[0] && image.planes[1] && image.planes[2]);
        const ov::Shape y_shape{1, size_t(image.height), size_t(image.width), 1};
        const ov::Shape u_shape{1, size_t(image.height / 2), size_t(image.width / 2), 1};
        const ov::Shape &v_shape{u_shape};

        ov::Tensor y_tensor{ov::element::u8, y_shape, image.planes[0]};
        ov::Tensor u_tensor{ov::element::u8, u_shape, image.planes[1]};
        ov::Tensor v_tensor{ov::element::u8, v_shape, image.planes[2]};

        // ROI
        if (image_has_roi(image)) {
            const auto &r = image.rect;
            ov::Coordinate begin{0, r.y, r.x, 0};
            ov::Coordinate end{y_shape[0], r.y + r.height, r.x + r.width, y_shape[3]};

            y_tensor = ov::Tensor(y_tensor, begin, end);

            begin[1] /= 2;
            begin[2] /= 2;
            end[1] /= 2;
            end[2] /= 2;

            u_tensor = ov::Tensor(u_tensor, begin, end);
            v_tensor = ov::Tensor(v_tensor, begin, end);
        }

        return {y_tensor, u_tensor, v_tensor};
    }

    static size_t get_channels_num(int format) {
        switch (format) {
        case FourCC::FOURCC_BGRA:
        case FourCC::FOURCC_BGRX:
        case FourCC::FOURCC_RGBA:
        case FourCC::FOURCC_RGBX:
            return 4;
        case FourCC::FOURCC_BGR:
        case FourCC::FOURCC_RGBP:
        case FourCC::FOURCC_BGRP:
            return 3;
        }
        return 0;
    }

    // Singleton core object
    static ov::Core &core() {
        static ov::Core ovcore;
        return ovcore;
    }

  protected:
    std::shared_ptr<ov::Model> _model;
    std::string _model_format;
    std::string _device;
    std::string _image_input_name;
    dlstreamer::ContextPtr _app_context;
    dlstreamer::OpenVINOContextPtr _openvino_context;
    ov::CompiledModel _compiled_model;
    MemoryType _memory_type;
#ifdef ENABLE_D3D_NPU_COLOR_CONV
    InferenceBackend::ImagePreprocessorType _pp_type;
#endif
    int _nireq = 0;
    int _batch_size = 0;

    size_t _origin_model_in_w = 0;
    size_t _origin_model_in_h = 0;
    bool _was_resize = false;

    ImageInference::CallbackFunc _callback;
    ImageInference::ErrorHandlingFunc _error_handler;

    void configure_model(const ConfigHelper &config) {

        auto [reshape_width, reshape_height] = config.reshape_size();
        if (config.need_reshape() && (reshape_width || reshape_height))
            reshape_model(reshape_height, reshape_width);

        auto ppp = ov::preprocess::PrePostProcessor(_model);
        configure_model_inputs(config, ppp);
        _model = ppp.build();
        _model_format = config.model_format();

        // dynamic shapes may have height=0 and width=0 after IE preprocessing
        // set lower and upper bound of dynamic shapes to match original frame dimensions
        auto [img_width, img_height] = config.image_size();
        if (img_width == 0 && img_height == 0 && config.pp_type() == ImagePreprocessorType::IE) {
            auto [frame_width, frame_height] = config.frame_size();
            reshape_model(frame_height, frame_width);
        }

        _batch_size = config.batch_size();
        if (_batch_size == 0) {
            try {
                _batch_size = core().get_property(config.device(), ov::optimal_batch_size);
            } catch (...) {
                _batch_size = 1; // Fallback if optimal batch size property is not supported
            }
        }

        GVA_DEBUG("Setting batch size of %d to model", _batch_size);
        ov::set_batch(_model, _batch_size);

        GVA_DEBUG("Model inputs after configuration:");
        size_t idx = 0;
        for (auto &input : _model->inputs()) {
            std::string debug_string = fmt::format("  [{}]: {}, shape: {}", idx++, fmt::join(input.get_names(), " "),
                                                   input.get_partial_shape().to_string());
            GVA_DEBUG("%s", debug_string.c_str());
        }

        // FIXME:
        auto item = _model->inputs().front();
        _image_input_name = item.get_any_name();
    }

    void configure_model_inputs(const ConfigHelper &config, ov::preprocess::PrePostProcessor &preproc) {
        const auto &inputs = _model->inputs();

        const ConfigHelper::InputsConfig inputs_cfg = config.inputs_cfg();
        print_inputs_config(inputs_cfg);

        if (inputs.size() == 1 && inputs_cfg.size() > 1)
            throw std::runtime_error(
                fmt::format("Model has 1 input layer, but input layer config has {} entries", inputs_cfg.size()));

        for (size_t i = 0; i < inputs.size(); i++) {
            const auto &item = inputs[i];

            const auto cmp_pred = [&](const auto &pair) { return item.get_names().count(pair.first) != 0; };
            auto it = std::find_if(inputs_cfg.begin(), inputs_cfg.end(), cmp_pred);
            ConfigHelper::InputConfig in_cfg;
            if (it != inputs_cfg.end()) {
                in_cfg = it->second;
            } else if (inputs.size() == 1) {
                // Single input and no external input config. Assuming this is an image input
                in_cfg.data_format = InferenceBackend::KEY_image;
                in_cfg.type = ov::element::u8;
            } else {
                GVA_ERROR("Input layer configuration doesn't contain info for input: %s", item.get_any_name().c_str());
                throw std::runtime_error("Config for layer precision does not contain precision info for layer: " +
                                         item.get_any_name());
            }

            auto &in = preproc.input(item.get_any_name());
            if (in_cfg.type != ov::element::undefined)
                in.tensor().set_element_type(in_cfg.type);

            if (in_cfg.data_format == InferenceBackend::KEY_image) {
                _image_input_name = item.get_any_name();
                GVA_DEBUG("Found image input: %s, layout: %s", _image_input_name.c_str(),
                          get_ov_node_layout(item).to_string().c_str());
                configure_image_input(config, in, in_cfg, item);
            }
        }

        std::stringstream ppp_ss;
        ppp_ss << preproc;
        GVA_DEBUG("%s", ppp_ss.str().c_str());
    }

    static ov::Layout get_image_layout_from_shape(const ov::PartialShape &shape) {
        // Check for layout type based on shape size and max number channels
        switch (shape.size()) {
        case 3:
            if (shape[2].get_max_length() <= 4 && shape[0].get_max_length() > 4)
                return "HWC";
            else
                return "CHW";
        case 4:
            if (shape[3].get_max_length() <= 4 && shape[1].get_max_length() > 4)
                return "NHWC";
            else
                return "NCHW";
        default:
            break;
        }
        return ov::Layout{};
    }

    /// @brief Get layout for input node
    /// @param node input node
    /// @param from_shape_fallback if the node has no layout information, try determining the layout from the node
    /// shape
    /// @return layout of node or empty layout
    ov::Layout get_ov_node_layout(const ov::Output<ov::Node> &node, bool from_shape_fallback = false) {
        ov::Layout result;
        if (auto ptr = dynamic_cast<const ov::op::v0::Parameter *>(node.get_node()))
            result = ptr->get_layout();
        else
            GVA_ERROR("Node '%s': couldn't downcast node to parameter", node.get_any_name().c_str());

        if (!result.empty() || !from_shape_fallback)
            return result;

        // Try determining from shape
        const auto node_name = node.get_any_name();
        const auto &pshape = node.get_partial_shape();
        result = get_image_layout_from_shape(pshape);
        if (result.empty())
            GVA_WARNING("Node '%s': couldn't determine layout for shape %s", node_name.c_str(),
                        pshape.to_string().c_str());
        else
            GVA_INFO("Node '%s': got layout %s for shape %s", node_name.c_str(), result.to_string().c_str(),
                     pshape.to_string().c_str());
        return result;
    }

    void configure_image_input(const ConfigHelper &config, ov::preprocess::InputInfo &input,
                               const ConfigHelper::InputConfig &input_config, const ov::Output<ov::Node> &node) {
        using InferenceBackend::ImagePreprocessorType;
        const auto pp_type = config.pp_type();
        std::string pp_type_string = fmt::format("Pre-processing: {}", pp_type);
        GVA_DEBUG("%s", pp_type_string.c_str());

        // OPENCV and VAAPI pre-processors handle color coversion and scaling, input tensors in NCHW format
        if (pp_type == ImagePreprocessorType::OPENCV || pp_type == ImagePreprocessorType::VAAPI_SYSTEM ||
            pp_type == ImagePreprocessorType::D3D11) {
            input.tensor().set_layout("NCHW");
        }

#ifdef ENABLE_D3D_NPU_COLOR_CONV
        if (pp_type == ImagePreprocessorType::D3D11) {
            input.tensor().set_layout("NHWC");
            if (_model_format == "BGR") {
                input.tensor().set_color_format(ov::preprocess::ColorFormat::BGRX);
                input.preprocess().convert_color(ov::preprocess::ColorFormat::BGR);
            } else {
                input.tensor().set_color_format(ov::preprocess::ColorFormat::RGBX);
                input.preprocess().convert_color(ov::preprocess::ColorFormat::RGB);
            }
        }
#endif
        // OV preprocessing is configured only for IE or VAAPI_SURFACE_SHARING
        if (pp_type == ImagePreprocessorType::VAAPI_SURFACE_SHARING || pp_type == ImagePreprocessorType::IE) {

            // IE and VAAPI_SURFACE_SHARING assume input tensors in NHWC format
            const auto color_format_pair = get_ov_color_format(config.image_format());
            input.tensor().set_color_format(color_format_pair.first, color_format_pair.second);
            input.tensor().set_layout("NHWC");

            // VAAPI_SURFACE_SHARING pre-processor requires NV12 input surface in GPU memory
            if (pp_type == ImagePreprocessorType::VAAPI_SURFACE_SHARING) {
                assert(_memory_type == MemoryType::VAAPI);
                assert(color_format_pair.first == ov::preprocess::ColorFormat::NV12_TWO_PLANES);
                input.tensor().set_memory_type(ov::intel_gpu::memory_type::surface);
            }

            // Convert input tensor color space into model color space if necessary
            const auto model_format_pair = get_ov_color_format(config.model_format());
            if (color_format_pair.first != model_format_pair.first)
                input.preprocess().convert_color(model_format_pair.first);

            // Check if image resize is required (IE-only, VAAPI_SURFACE_SHARING does image resize)
            if (pp_type == ImagePreprocessorType::IE) {
                // System memory input
                assert(_memory_type == MemoryType::SYSTEM);
                // Validate _was_resize flag. We should not resize a model twice.
                assert(_was_resize == false);

                auto [img_width, img_height] = config.image_size();
                bool apply_resize = img_width != _origin_model_in_w || img_height != _origin_model_in_h;

                if (img_width && img_height) {
                    input.tensor().set_spatial_static_shape(img_height, img_width);
                } else {
                    // Dynamic input, e.g. ROI
                    input.tensor().set_spatial_dynamic_shape();
                }

                if (apply_resize) {
                    input.preprocess().resize(ov::preprocess::ResizeAlgorithm::RESIZE_LINEAR);
                    _was_resize = true;
                }
            }
        }

        // Input tensors are u8, convert to model precision if necessary
        auto node_element_type = node.get_element_type();
        if (node_element_type != input_config.type)
            input.preprocess().convert_element_type(node_element_type);

        // If defined, scale the image by the defined value
        auto mean = config.pixel_value_mean();
        auto scale = config.pixel_value_scale();

        if (mean.size() == 1) {
            input.preprocess().mean(mean[0]);
        } else if (mean.size() >= 1) {
            input.preprocess().mean(mean);
        }

        if (scale.size() == 1) {
            input.preprocess().scale(scale[0]);
        } else if (scale.size() > 1) {
            input.preprocess().scale(scale);
        }

        // OV preprocessor does implicit layout conversion. If original layout is unknown, assume it is NCHW.
        ov::Layout model_layout = get_ov_node_layout(node);
        if (model_layout.empty()) {
            // Need to specify H and W dimensions in model, others are not important
            model_layout = "??HW";
            GVA_WARNING("Layout for '%s' input is not explicitly set, so it's defaulted to %s",
                        node.get_any_name().c_str(), model_layout.to_string().c_str());
            input.model().set_layout(model_layout);
        }
    }

    void reshape_model(size_t image_height, size_t image_width) {
        std::map<ov::Output<ov::Node>, ov::PartialShape> port_to_shape;

        for (const ov::Output<ov::Node> &input : _model->inputs()) {
            ov::PartialShape shape = input.get_partial_shape();

            const ov::Layout layout = get_ov_node_layout(input, true);
            if (layout.empty())
                throw std::runtime_error(fmt::format("Reshape: couldn't determine input layout"));

            if (input.get_partial_shape().is_static()) {
                if (image_height > 0)
                    shape[ov::layout::height_idx(layout)] = image_height;
                if (image_width > 0)
                    shape[ov::layout::width_idx(layout)] = image_width;
            } else {
                shape[ov::layout::height_idx(layout)] = ov::Dimension(1, image_height);
                shape[ov::layout::width_idx(layout)] = ov::Dimension(1, image_width);
            }

            GVA_INFO("Reshaping model input %s from %s to %s", input.get_any_name().c_str(),
                     input.get_partial_shape().to_string().c_str(), shape.to_string().c_str());
            port_to_shape[input] = shape;
        }
        _model->reshape(port_to_shape);
        print_input_and_outputs_info(*_model);
    }

    // Loads network to the device
    void load_network(const ConfigHelper &config) {
        assert(!_compiled_model);
        ov::AnyMap ov_params = config.inference_cfg();
        std::string params = fmt::format("Params for compile_model:\n  {}", fmt::join(ov_params, "\n  "));
        GVA_INFO("%s", params.c_str());
        // adjust_ie_config(ov_params);

        GVA_INFO("Loading network to device %s", _device.c_str());
        if (_openvino_context) {
            GVA_INFO("using remote context");
        }

        // print_input_and_outputs_info(*_model);
        if (_openvino_context) {
            _compiled_model = core().compile_model(_model, _openvino_context->remote_context(), ov_params);
        } else {
            _compiled_model = core().compile_model(_model, _device, ov_params);
        }
        GVA_INFO("Network loaded to device");

        auto supported_properties = _compiled_model.get_property(ov::supported_properties);
        for (const auto &cfg : supported_properties) {
            if (cfg == ov::supported_properties)
                continue;
            auto prop = _compiled_model.get_property(cfg);
            GVA_DEBUG(" %s: %s", cfg.c_str(), prop.as<std::string>().c_str());
        }
    }

    static std::pair<ov::preprocess::ColorFormat, std::vector<std::string>>
    get_ov_color_format(const std::string &img_format) {
        using namespace ov::preprocess;
        static const std::map<std::string, std::pair<ov::preprocess::ColorFormat, std::vector<std::string>>> format_map{
            {"NV12", {ColorFormat::NV12_TWO_PLANES, {"y", "uv"}}},
            {"I420", {ColorFormat::I420_THREE_PLANES, {"y", "u", "v"}}},
            {"RGB", {ColorFormat::RGB, {}}},
            {"BGR", {ColorFormat::BGR, {}}},
            {"RGBX", {ColorFormat::RGBX, {}}},
            {"BGRX", {ColorFormat::BGRX, {}}},
            {"RGBA", {ColorFormat::RGBX, {}}},
            {"BGRA", {ColorFormat::BGRX, {}}}};
        auto iter = format_map.find(img_format);
        if (iter != format_map.end()) {
            return iter->second;
        }

        GVA_ERROR("Color format '%s' is not supported by Inference Engine preprocessing. UNDEFINED will be set",
                  img_format.c_str());
        return {ov::preprocess::ColorFormat::UNDEFINED, {}};
    }

    bool is_device_gpu() const {
        return _device.find("GPU") != std::string::npos;
    }

    bool is_device_multi() const {
        return _device.find("MULTI") != std::string::npos;
    }

    dlstreamer::ContextPtr create_remote_context() {
        // FIXME: invert to reduce nesting
        if (is_device_gpu() && !is_device_multi()) {
#ifdef _WIN32
            if (_memory_type == MemoryType::D3D11 || _memory_type == MemoryType::SYSTEM) {
                if (_app_context) {
                    try {
                        dlstreamer::D3D11ContextPtr d3d11_ctx = dlstreamer::D3D11Context::create(_app_context);
                        _openvino_context = std::make_shared<dlstreamer::OpenVINOContext>(core(), _device, d3d11_ctx);
                    } catch (std::exception &e) {
                        GVA_ERROR("Exception occurred when creating OpenVINO™ toolkit remote context: %s", e.what());
                        std::throw_with_nested(std::runtime_error("couldn't create OV remote context"));
                    }

                } else if (_memory_type == MemoryType::D3D11) {
                    throw std::runtime_error("Display must be provided for GPU device with d3d11 backend");
                }
            }
#else
            if (_memory_type == MemoryType::VAAPI || _memory_type == MemoryType::SYSTEM) {
                if (_app_context) {
                    try {
                        dlstreamer::VAAPIContextPtr vaapi_ctx = dlstreamer::VAAPIContext::create(_app_context);
                        _openvino_context = std::make_shared<dlstreamer::OpenVINOContext>(core(), _device, vaapi_ctx);
                    } catch (std::exception &e) {
                        GVA_ERROR("Exception occurred when creating OpenVINO™ toolkit remote context: %s", e.what());
                        std::throw_with_nested(std::runtime_error("couldn't create OV remote context"));
                    }

                } else if (_memory_type == MemoryType::VAAPI) {
                    throw std::runtime_error(
                        "Display must be provided for GPU device with vaapi-surface-sharing backend");
                }
            }
#endif
        }
        return _openvino_context;
    }
};

void OpenVINOImageInference::SetCompletionCallback(std::shared_ptr<BatchRequest> &batch_request) {
    assert(batch_request && "Batch request is null");

    auto cb = [=, this](std::exception_ptr ex) {
        ITT_TASK("completion_callback_lambda_new");

        try {
            if (ex) {
                std::string ex_string = fmt::format("exception occured during inference: {}", ex);
                GVA_ERROR("%s", ex_string.c_str());
                this->handleError(batch_request->buffers);
            } else {
                this->WorkingFunction(batch_request);
            }
        } catch (const std::exception &e) {
            GVA_ERROR("An error occurred at inference request completion callback [new]:\n%s",
                      Utils::createNestedErrorMsg(e).c_str());
        }

        FreeRequest(batch_request);
    };
    batch_request->infer_request_new.set_callback(cb);
}

OpenVINOImageInference::OpenVINOImageInference(const InferenceBackend::InferenceConfig &config, Allocator *,
                                               dlstreamer::ContextPtr context, CallbackFunc callback,
                                               ErrorHandlingFunc error_handler, MemoryType memory_type)
    : context_(context), memory_type(memory_type), callback(callback), handleError(error_handler),
      batch_size(std::stoi(config.at(KEY_BASE).at(KEY_BATCH_SIZE))), requests_processing_(0U) {

    try {
        ConfigHelper cfg_helper(config);
        const auto pp_type = cfg_helper.pp_type();
#ifndef ENABLE_D3D_NPU_COLOR_CONV
        _impl = std::make_unique<OpenVinoNewApiImpl>(cfg_helper, context, callback, error_handler, memory_type);
#else
        _impl =
            std::make_unique<OpenVinoNewApiImpl>(cfg_helper, context, callback, error_handler, memory_type, pp_type);
#endif

        model_name = _impl->_model->get_friendly_name();
        nireq = _impl->_nireq;
        batch_size = _impl->_batch_size;
        image_layer = _impl->_image_input_name;

        for (int i = 0; i < nireq; i++) {
            std::shared_ptr<BatchRequest> batch_request = std::make_shared<BatchRequest>();
            batch_request->infer_request_new = _impl->_compiled_model.create_infer_request();
            batch_request->in_tensors.resize(_impl->_model->inputs().size());
            SetCompletionCallback(batch_request);
            freeRequests.push(batch_request);
        }

#ifndef ENABLE_D3D_NPU_COLOR_CONV
        if (pp_type == InferenceBackend::ImagePreprocessorType::OPENCV ||
            pp_type == InferenceBackend::ImagePreprocessorType::D3D11) {
#else
        if (pp_type == InferenceBackend::ImagePreprocessorType::OPENCV) {
#endif
            std::string pp_type_string = fmt::format("creating pre-processor, type: {}", pp_type);
            GVA_INFO("%s", pp_type_string.c_str());
            const std::string custom_preproc_lib = cfg_helper.custom_preproc_lib();
            pre_processor.reset(InferenceBackend::ImagePreprocessor::Create(pp_type, custom_preproc_lib));
        }

    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to construct OpenVINOImageInference"));
    }
}

OpenVINOImageInference::~OpenVINOImageInference() {
    GVA_DEBUG("Image Inference destruct");
    Close();
}

void OpenVINOImageInference::FreeRequest(std::shared_ptr<BatchRequest> request) {
    const size_t buffer_size = request->buffers.size();
    request->buffers.clear();
    for (auto &in_vec : request->in_tensors) {
        in_vec.clear();
    }
    freeRequests.push(request);
    requests_processing_ -= buffer_size;
    request_processed_.notify_all();
}

bool OpenVINOImageInference::IsQueueFull() {
    return freeRequests.empty();
}

Image fill_image(ov::Tensor &tensor, size_t bindex) {
    Image image = Image();
    const auto &dims = tensor.get_shape();
    if (dims.size() < 4)
        throw std::runtime_error("Unsupported layout: dims size less than 4");
    if (dims.size() < 2 || dims[1] > 4)
        throw std::runtime_error("Unsupported layout: NCHW is expected");
    image.width = safe_convert<uint32_t>(dims[3]);
    image.height = safe_convert<uint32_t>(dims[2]);
    if (bindex >= dims[0]) {
        throw std::out_of_range("Image index is out of range in batch blob");
    }
    auto elem_type = tensor.get_element_type();
    size_t plane_size = safe_mul(size_t(safe_mul(image.width, image.height)), elem_type.size());
    size_t buffer_offset = safe_mul(safe_mul(bindex, plane_size), dims[1]);

    image.planes[0] = static_cast<uint8_t *>(tensor.data()) + buffer_offset;
    image.planes[1] = image.planes[0] + plane_size;
    image.planes[2] = image.planes[1] + plane_size;
    image.planes[3] = nullptr;

    image.stride[0] = image.width;
    image.stride[1] = image.width;
    image.stride[2] = image.width;
    image.stride[3] = 0;
    return image;
}

Image map_ov_tensor_to_img(ov::Tensor &tensor, size_t batch_index) {
    ITT_TASK(__FUNCTION__);
    assert(tensor);

    Image image = fill_image(tensor, batch_index);
    switch (tensor.get_element_type()) {
    case ov::element::f32:
        image.format = FourCC::FOURCC_RGBP_F32;
        break;
    case ov::element::u8:
        image.format = FourCC::FOURCC_RGBP;
        break;
    default:
        throw std::runtime_error("Unsupported precision");
    }
    return image;
}

void OpenVINOImageInference::SubmitImageProcessing(const std::string &input_name, std::shared_ptr<BatchRequest> request,
                                                   const Image &src_img, const InputImageLayerDesc::Ptr &pre_proc_info,
                                                   const ImageTransformationParams::Ptr image_transform_info) {
    ITT_TASK(__FUNCTION__);
    assert(request);

    // FIXME: single input
    if (request->in_tensors.front().empty()) {
        request->in_tensors.front().push_back(request->infer_request_new.get_tensor(input_name));
    }
    const size_t batch_index = request->buffers.size();
    Image dst_img = map_ov_tensor_to_img(request->in_tensors.front().front(), batch_index);
    if (src_img.planes[0] != dst_img.planes[0]) { // only convert if different buffers
        try {
            pre_processor->Convert(src_img, dst_img, pre_proc_info, image_transform_info);
        } catch (const std::exception &e) {
            std::throw_with_nested(std::runtime_error("Failed while software frame preprocessing"));
        }
    }
}

void OpenVINOImageInference::BypassImageProcessing(const std::string &input_name, std::shared_ptr<BatchRequest> request,
                                                   const Image &src_img, size_t batch_size) {
    ITT_TASK(__FUNCTION__);

    auto ov_tensor = _impl->image_to_tensors(src_img);

    if (batch_size > 1) {
        if (ov_tensor.size() != request->in_tensors.size())
            throw std::runtime_error("BypassImageProcessing - unexpected number of tensors!");
        size_t input_idx = 0;
        for (auto &t : ov_tensor) {
            request->in_tensors[input_idx].emplace_back(std::move(t));
            input_idx++;
        }

        if (request->in_tensors.front().size() == batch_size) {
            // FIXME: input name ?
            for (size_t i = 0; i < request->in_tensors.size(); i++) {
                // FIXME: move?
                request->infer_request_new.set_input_tensors(i, request->in_tensors[i]);
            }
        }
    } else {
        if (ov_tensor.size() == 1) {
            request->infer_request_new.set_tensor(input_name, ov_tensor.front());
        } else {
            // FIXME
            for (size_t i = 0; i < ov_tensor.size(); i++) {
                request->infer_request_new.set_input_tensor(i, ov_tensor[i]);
            }
        }
    }
}

bool OpenVINOImageInference::DoNeedImagePreProcessing(const InferenceBackend::ImagePtr image) {
    if (pre_processor.get() != nullptr)
        return true;

    if (image == nullptr)
        return false;

    // workaround for NPU plugin serialization when non-contiguous tensors submitted
    if ((_impl->_device.find("NPU") != std::string::npos) && (_impl->_memory_type == MemoryType::SYSTEM) &&
        ((image->format == FourCC::FOURCC_RGBP) || (image->format == FourCC::FOURCC_BGRP))) {

        bool contiguous = (image->planes[1] - image->planes[0] == image->width * image->height) &&
                          (image->planes[2] - image->planes[1] == image->width * image->height) &&
                          (image->stride[0] == image->stride[1]) && (image->stride[1] == image->stride[2]) &&
                          (image->stride[2] == image->width);

        if (!contiguous) {
            GVA_WARNING("Force OPENCV preprocessor to convert non-contiguous tensors into contigous memory location");
            pre_processor.reset(
                InferenceBackend::ImagePreprocessor::Create(InferenceBackend::ImagePreprocessorType::OPENCV, ""));
            return true;
        }
    }

    return false;
}

void OpenVINOImageInference::ApplyInputPreprocessors(
    std::shared_ptr<BatchRequest> &request, const std::map<std::string, InputLayerDesc::Ptr> &input_preprocessors) {
    ITT_TASK(__FUNCTION__);
    assert(request && "Batch request is null");

    for (const auto &preprocessor : input_preprocessors) {
        if (preprocessor.second == nullptr)
            continue;

        if (preprocessor.first == KEY_image) {
            if (!DoNeedImagePreProcessing(nullptr))
                continue;
        }

        const auto &model_inputs = _impl->_model->inputs();
        std::shared_ptr<OpenvinoInputTensor> intput_tensor = nullptr;
        if (model_inputs.size() == 1) {
            intput_tensor = std::make_shared<OpenvinoInputTensor>(request->infer_request_new.get_input_tensor());
        } else {
            intput_tensor =
                std::make_shared<OpenvinoInputTensor>(request->infer_request_new.get_tensor(preprocessor.second->name));
        }

        preprocessor.second->preprocessor(intput_tensor);
    }
}

void OpenVINOImageInference::SubmitImage(
    IFrameBase::Ptr frame, const std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> &input_preprocessors) {
    ITT_TASK(__FUNCTION__);

    if (!frame)
        throw std::invalid_argument("Invalid frame provided");

    std::unique_lock<std::mutex> lk(requests_mutex_);
    ++requests_processing_;
    std::shared_ptr<BatchRequest> request = freeRequests.pop();

    try {
        if (DoNeedImagePreProcessing(frame->GetImage())) {
            SubmitImageProcessing(
                image_layer, request, *frame->GetImage(),
                getImagePreProcInfo(input_preprocessors), // contain operations order for Custom Image PreProcessing
                frame->GetImageTransformationParams()     // during CIPP will be filling of crop and aspect-ratio
                                                          // parameters
            );
            // After running this function self-managed image memory appears, and the old image memory can be
            // released
            frame->SetImage(nullptr);
        } else {
            BypassImageProcessing(image_layer, request, *frame->GetImage(), safe_convert<size_t>(batch_size));
        }

        ApplyInputPreprocessors(request, input_preprocessors);

        request->buffers.push_back(frame);
    } catch (const std::exception &e) {
        GVA_ERROR("Pre-processing has failed: %s", e.what());
        std::throw_with_nested(std::runtime_error("Pre-processing was failed."));
    }

    try {
        // start inference asynchronously if enough buffers for batching
        if (request->buffers.size() >= safe_convert<size_t>(batch_size)) {
            request->start_async();
        } else {
            freeRequests.push_front(request);
        }
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Inference async start was failed."));
    }
}

const std::string &OpenVINOImageInference::GetModelName() const {
    return model_name;
}

size_t OpenVINOImageInference::GetBatchSize() const {
    return safe_convert<size_t>(batch_size);
}

size_t OpenVINOImageInference::GetNireq() const {
    return safe_convert<size_t>(nireq);
}

void OpenVINOImageInference::GetModelImageInputInfo(size_t &width, size_t &height, size_t &batch_size, int &format,
                                                    int &memory_type_) const {
    _impl->get_model_image_input_info(width, height, batch_size, format, memory_type_);
}

std::map<std::string, std::vector<size_t>> OpenVINOImageInference::GetModelInputsInfo() const {
    auto info = _impl->get_model_inputs_info();
    return info;
}

std::map<std::string, std::vector<size_t>> OpenVINOImageInference::GetModelOutputsInfo() const {
    auto info = _impl->get_model_outputs_info();
    return info;
}

std::map<std::string, GstStructure *> OpenVINOImageInference::GetModelInfoPostproc() const {
    auto info = _impl->get_model_info_postproc();
    return info;
}

std::map<std::string, GstStructure *> OpenVINOImageInference::GetModelInfoPreproc(const std::string model_file,
                                                                                  const gchar *pre_proc_config,
                                                                                  const gchar *ov_extension_lib) {
    auto info = OpenVinoNewApiImpl::get_model_info_preproc(model_file, pre_proc_config, ov_extension_lib);
    return info;
}

void OpenVINOImageInference::Flush() {
    ITT_TASK(__FUNCTION__);

    // because Flush can execute by several threads for one InferenceImpl instance
    // it must be synchronous.
    std::unique_lock<std::mutex> requests_lk(requests_mutex_);

    std::unique_lock<std::mutex> flush_lk(flush_mutex);

    while (requests_processing_ != 0) {
        auto request = freeRequests.pop();

        if (request->buffers.size() > 0) {
            try {
                // WA: Fill non-complete batch with last element. Can be removed once supported in OV
                if (batch_size > 1 && !DoNeedImagePreProcessing(nullptr)) {
                    size_t input_idx = 0;
                    for (auto &input_vec : request->in_tensors) {
                        for (int i = input_vec.size(); i < batch_size; i++)
                            input_vec.push_back(input_vec.back());
                        // FIXME: move?
                        request->infer_request_new.set_input_tensors(input_idx, input_vec);
                        input_idx++;
                    }
                }

                request->start_async();
            } catch (const std::exception &e) {
                GVA_ERROR("Couldn't start inferece on flush: %s", e.what());
                this->handleError(request->buffers);
                FreeRequest(request);
            }
        } else {
            freeRequests.push(request);
        }

        // wait_for unlocks flush_mutex until we get notify
        // waiting will be continued if requests_processing_ != 0
        request_processed_.wait_for(flush_lk, std::chrono::seconds(1), [this] { return requests_processing_ == 0; });
    }
}

void OpenVINOImageInference::Close() {
    Flush();
    while (!freeRequests.empty()) {
        auto req = freeRequests.pop();
        req->infer_request_new.set_callback([](std::exception_ptr) {});
    }
}

void OpenVINOImageInference::WorkingFunction(const std::shared_ptr<BatchRequest> &request) {
    assert(request);

    std::map<std::string, OutputBlob::Ptr> output_blobs;
    const auto &outputs = _impl->_compiled_model.outputs();
    for (size_t i = 0; i < outputs.size(); i++) {
        auto name = outputs[i].get_names().size() > 0 ? outputs[i].get_any_name() : std::string("output");
        output_blobs[name] = std::make_shared<OpenvinoOutputTensor>(request->infer_request_new.get_output_tensor(i));
    }
    callback(output_blobs, request->buffers);
}
