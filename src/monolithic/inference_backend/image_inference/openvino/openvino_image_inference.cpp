/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// OpenVINO backend of ImageInference. Contains:
// - OpenVinoModelAdapter: reads, configures and compiles the model for the requested device.
// - OpenVINOImageInference: pool of infer requests, batching, SubmitImage, completion callback and Flush.

#include "gst/gststructure.h"
#include "inference_backend/image_inference.h"
#include <spdlog/fmt/fmt.h>

#include <cstddef>
#include <openvino/runtime/properties.hpp>

#include <openvino/runtime/intel_npu/level_zero/level_zero.hpp>
#include <openvino/runtime/intel_npu/properties.hpp>

#include <dlstreamer/openvino/context.h>
#ifdef _WIN32
#include <dlstreamer/d3d11/context.h>
#endif
// For logger_name
#include <dlstreamer/element.h>

#include <spdlog/fmt/bundled/ranges.h>

#include "model_api_converters.h"
#include "openvino_config_helper.h"
#include "openvino_core.h"
#include "openvino_formatters.h"
#include "openvino_image_inference.h"
#include "openvino_image_to_tensor_converter.h"

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
#include <mutex>
#include <regex>
#include <stdio.h>
#include <thread>
#include <unordered_map>

using namespace InferenceBackend;
using namespace InferenceBackend::OpenVino;

namespace {

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

} // namespace

class OpenVinoModelAdapter {
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
    OpenVinoModelAdapter(const ConfigHelper &config, dlstreamer::ContextPtr context,
                         ImageInference::CallbackFunc callback, ImageInference::ErrorHandlingFunc error_handler,
                         MemoryType memory_type)
        : _app_context(std::move(context)), _memory_type(memory_type), _callback(callback),
          _error_handler(error_handler) {
#else
    OpenVinoModelAdapter(const ConfigHelper &config, dlstreamer::ContextPtr context,
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
            openvino_core().add_extension(ov_extension_lib);
        }

        // read model & configure model
        _model_path = config.model_path();
        _model = openvino_core().read_model(_model_path);

        {
            size_t bs;
            int fmt, mt;
            get_model_image_input_info(_origin_model_in_w, _origin_model_in_h, bs, fmt, mt);
        }

        configure_model(config);
        create_remote_context();
        _img_to_tensor = std::make_unique<ImageToTensorConverter>(_openvino_context);

        // Load nn to device
        load_network(config);

        if (!_nireq)
            _nireq = _compiled_model.get_property(ov::optimal_number_of_infer_requests);
        GVA_DEBUG("Num of inference req: %d", _nireq);
    }

    ~OpenVinoModelAdapter() {
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
        case MemoryType::DMA_BUFFER:
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

  protected:
    std::shared_ptr<ov::Model> _model;
    std::string _model_path;
    std::string _model_format;
    std::string _device;
    std::string _image_input_name;
    dlstreamer::ContextPtr _app_context;
    dlstreamer::OpenVINOContextPtr _openvino_context;
    ov::CompiledModel _compiled_model;
    MemoryType _memory_type;

    // Created in the constructor once the (optional) GPU remote context exists.
    std::unique_ptr<ImageToTensorConverter> _img_to_tensor;
#ifdef ENABLE_D3D_NPU_COLOR_CONV
    InferenceBackend::ImagePreprocessorType _pp_type;
#endif
    int _nireq = 0;
    int _auto_batch_num_requests = 0;
    int _batch_size = 0;
    int _batch_timeout = -1;

    size_t _origin_model_in_w = 0;
    size_t _origin_model_in_h = 0;
    bool _was_resize = false;

    ImageInference::CallbackFunc _callback;
    ImageInference::ErrorHandlingFunc _error_handler;

    void configure_model(const ConfigHelper &config) {
        auto [reshape_width, reshape_height] = config.reshape_size();
        if (config.need_reshape() && (reshape_width || reshape_height))
            reshape_model(config, reshape_height, reshape_width, config.reshape_static());

        auto ppp = ov::preprocess::PrePostProcessor(_model);
        configure_model_inputs(config, ppp);
        _model = ppp.build();
        _model_format = config.model_format();

        // dynamic shapes may have height=0 and width=0 after IE preprocessing
        // set lower and upper bound of dynamic shapes to match original frame dimensions
        auto [img_width, img_height] = config.image_size();
        if (img_width == 0 && img_height == 0 && config.pp_type() == ImagePreprocessorType::IE) {
            auto [frame_width, frame_height] = config.frame_size();
            reshape_model(config, frame_height, frame_width, false);
        }

        _batch_size = config.batch_size();
        if (_batch_size == 0)
            _batch_size = OpenVINOImageInference::GetDeviceOptimalBatchSize(config.device());

        _batch_timeout = config.batch_timeout();

        if (_batch_timeout > -1) {
            _auto_batch_num_requests = _batch_size;
            _batch_size = 1;
        }

        GVA_DEBUG("Setting batch size of %d to model", _batch_size);
        // Every input carries an 'N' layout by now (image via configure_image_input, aux inputs tagged
        // in configure_model_inputs), so set_batch pins the batch axis on all of them.
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
            if (in_cfg.type != ov::element::dynamic)
                in.tensor().set_element_type(in_cfg.type);

            if (in_cfg.data_format == InferenceBackend::KEY_image) {
                _image_input_name = item.get_any_name();
                GVA_DEBUG("Found image input: %s, layout: %s", _image_input_name.c_str(),
                          get_ov_node_layout(item).to_string().c_str());
                configure_image_input(config, in, in_cfg, item);
            } else {
                // Auxiliary (non-image) input (e.g. MonoDETR's calib/img_sizes): tag a batch ('N')
                // layout so the later ov::set_batch can pin its batch axis. Only when axis 0 is dynamic
                // and no layout is set, to avoid rewriting a legitimately fixed leading dimension.
                const auto &pshape = item.get_partial_shape();
                if (get_ov_node_layout(item).empty() && pshape.rank().is_static() && pshape.size() > 0 &&
                    pshape[0].is_dynamic())
                    in.model().set_layout(ov::Layout("N" + std::string(pshape.size() - 1, '?')));
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

        // Layout lifecycle (why there are two set_layout calls in this function):
        //   - input.tensor().set_layout(...)  = the TENSOR layout, i.e. the axis order of the
        //     incoming data buffer (NCHW for OPENCV/VAAPI, NHWC for IE/surface-sharing). Set above
        //     depending on pp_type.
        //   - input.model().set_layout(...)   = a HINT about the original network body's layout,
        //     used by ppp.build() only to decide whether to insert a transpose.
        // ppp.build() (in configure_model) reconciles the two: the resulting Parameter ends up
        // carrying the TENSOR layout, and that is what ov::set_batch later reads. Since both NCHW
        // and NHWC put 'N' at axis 0, set_batch always finds the batch on the image input.
        // The "??HW" hint below is transient: only H/W placement matters for the model side, so we
        // leave N/C unspecified. It does not become the Parameter's final layout.
        // NOTE: this whole function only runs for the image input (data_format == KEY_image); aux
        // inputs (e.g. calib/img_sizes) get their batch ('N') layout tagged in configure_model_inputs
        // instead, so ov::set_batch can pin their batch axis too.
        ov::Layout model_layout = get_ov_node_layout(node);
        if (model_layout.empty()) {
            // Need to specify H and W dimensions in model, others are not important
            model_layout = "??HW";
            GVA_WARNING("Layout for '%s' input is not explicitly set, so it's defaulted to %s",
                        node.get_any_name().c_str(), model_layout.to_string().c_str());
            input.model().set_layout(model_layout);
        }
    }

    static size_t get_model_channels_num(const ConfigHelper &config) {
        const auto &model_format = config.model_format();
        if (model_format == "GRAYSCALE")
            return 1;
        return 3;
    }

    bool is_image_input(const ConfigHelper &config, const ov::Output<ov::Node> &input) const {
        const auto inputs_cfg = config.inputs_cfg();
        const auto cmp_pred = [&](const auto &pair) { return input.get_names().count(pair.first) != 0; };
        auto it = std::find_if(inputs_cfg.begin(), inputs_cfg.end(), cmp_pred);
        if (it != inputs_cfg.end())
            return it->second.data_format == InferenceBackend::KEY_image;

        return _model->inputs().size() == 1;
    }

    void reshape_model(const ConfigHelper &config, size_t image_height, size_t image_width, bool reshape_static) {
        std::map<ov::Output<ov::Node>, ov::PartialShape> port_to_shape;

        for (const ov::Output<ov::Node> &input : _model->inputs()) {
            ov::PartialShape shape = input.get_partial_shape();

            const ov::Layout layout = get_ov_node_layout(input, true);
            if (layout.empty())
                throw std::runtime_error(fmt::format("Reshape: couldn't determine input layout"));

            if (input.get_partial_shape().is_static() || reshape_static) {
                if (image_height > 0)
                    shape[ov::layout::height_idx(layout)] = image_height;
                if (image_width > 0)
                    shape[ov::layout::width_idx(layout)] = image_width;
            } else {
                shape[ov::layout::height_idx(layout)] = ov::Dimension(1, image_height);
                shape[ov::layout::width_idx(layout)] = ov::Dimension(1, image_width);
            }

            if (is_image_input(config, input)) {
                const auto channels_idx = ov::layout::channels_idx(layout);
                if (channels_idx >= 0 && shape[channels_idx].is_dynamic())
                    shape[channels_idx] = get_model_channels_num(config);
            }

            GVA_INFO("Reshaping model input %s from %s to %s", input.get_any_name().c_str(),
                     input.get_partial_shape().to_string().c_str(), shape.to_string().c_str());
            port_to_shape[input] = shape;
        }
        _model->reshape(port_to_shape);
        print_input_and_outputs_info(*_model);
    }

    // NPU zero-copy DMA-BUFs use a padded row pitch when the model input width is not 64-byte aligned.
    // Let the NPU consume such strided input tensors directly instead of copying them on the CPU.
    void enable_npu_dmabuf_strides(ov::AnyMap &ov_params) {
        if (_memory_type != MemoryType::DMA_BUFFER || _device.find("NPU") == std::string::npos ||
            _image_input_name.empty() || ov_params.count(ov::intel_npu::enable_strides_for.name()))
            return;

        size_t width = 0, height = 0, batch = 0;
        int format = 0, memory_type = 0;
        get_model_image_input_info(width, height, batch, format, memory_type);
        if (width == 0 || alignDmaBufPitch(safe_convert<uint32_t>(width)) == width)
            return;

        GVA_INFO("NPU zero-copy: model input width %zu is not %u-byte aligned, enabling strided input '%s'", width,
                 DMA_BUF_PITCH_ALIGNMENT, _image_input_name.c_str());
        ov_params[ov::intel_npu::enable_strides_for.name()] = _image_input_name;
    }

    // Loads network to the device
    void load_network(const ConfigHelper &config) {
        assert(!_compiled_model);
        ov::AnyMap ov_params = config.inference_cfg();

        // Check if enabling strided input for NPU zero-copy is applicable and required.
        enable_npu_dmabuf_strides(ov_params);

        std::string params = fmt::format("Params for compile_model:\n  {}", fmt::join(ov_params, "\n  "));
        GVA_INFO("%s", params.c_str());

        GVA_INFO("Loading network to device %s", _device.c_str());
        if (_openvino_context) {
            GVA_INFO("using remote context");
        }

        // print_input_and_outputs_info(*_model);
        try {
            if (_openvino_context) {
                _compiled_model = openvino_core().compile_model(_model, _openvino_context->remote_context(), ov_params);
            } else {
                std::string formatted_device = _device;
                if (_batch_timeout > -1) {
                    formatted_device = fmt::format("BATCH:{}({})", _device, _auto_batch_num_requests);
                }
                _compiled_model = openvino_core().compile_model(_model, formatted_device, ov_params);
            }
        } catch (const std::exception &e) {
            GVA_ERROR("Failed to compile model for device %s: %s", _device.c_str(), e.what());
            std::throw_with_nested(std::runtime_error("Failed to compile model for device " + _device));
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
                        _openvino_context =
                            std::make_shared<dlstreamer::OpenVINOContext>(openvino_core(), _device, d3d11_ctx);
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
                        _openvino_context =
                            std::make_shared<dlstreamer::OpenVINOContext>(openvino_core(), _device, vaapi_ctx);
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
        ITT_TASK("completion_callback");

        try {
            if (ex) {
                std::string ex_string = fmt::format("exception occured during inference: {}", ex);
                GVA_ERROR("%s", ex_string.c_str());
                this->handleError(batch_request->buffers);
            } else {
                this->WorkingFunction(batch_request);
            }
        } catch (const std::exception &e) {
            GVA_ERROR("An error occurred at inference request completion callback:\n%s",
                      Utils::createNestedErrorMsg(e).c_str());
        }

        FreeRequest(batch_request);
    };
    batch_request->infer_request.set_callback(cb);
}

OpenVINOImageInference::OpenVINOImageInference(const InferenceBackend::InferenceConfig &config, Allocator *,
                                               dlstreamer::ContextPtr context, CallbackFunc callback,
                                               ErrorHandlingFunc error_handler, MemoryType memory_type)
    : context_(context), memory_type(memory_type), callback(callback), handleError(error_handler),
      batch_size(std::stoi(config.at(KEY_BASE).at(KEY_BATCH_SIZE))), batch_timeout(-1), requests_processing_(0U) {

    try {
        if (config.count(KEY_INFERENCE) > 0) {
            const auto &inference_config = config.at(KEY_INFERENCE);
            if (inference_config.count(ov::auto_batch_timeout.name()) > 0) {
                batch_timeout = std::stoi(inference_config.at(ov::auto_batch_timeout.name()));
            }
        }

        ConfigHelper cfg_helper(config);
        const auto pp_type = cfg_helper.pp_type();
#ifndef ENABLE_D3D_NPU_COLOR_CONV
        _impl = std::make_unique<OpenVinoModelAdapter>(cfg_helper, context, callback, error_handler, memory_type);
#else
        _impl =
            std::make_unique<OpenVinoModelAdapter>(cfg_helper, context, callback, error_handler, memory_type, pp_type);
#endif

        model_name = _impl->_model->get_friendly_name();
        nireq = _impl->_nireq;
        batch_size = _impl->_batch_size;
        batch_timeout = _impl->_batch_timeout;
        image_layer = _impl->_image_input_name;

        for (int i = 0; i < nireq; i++) {
            std::shared_ptr<BatchRequest> batch_request = std::make_shared<BatchRequest>();
            batch_request->infer_request = _impl->_compiled_model.create_infer_request();
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
        request->in_tensors.front().push_back(request->infer_request.get_tensor(input_name));
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

    auto ov_tensor = _impl->_img_to_tensor->image_to_tensors(src_img);

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
                request->infer_request.set_input_tensors(i, request->in_tensors[i]);
            }
        }
    } else {
        if (ov_tensor.size() == 1) {
            request->infer_request.set_tensor(input_name, ov_tensor.front());
        } else {
            // FIXME
            for (size_t i = 0; i < ov_tensor.size(); i++) {
                request->infer_request.set_input_tensor(i, ov_tensor[i]);
            }
        }
    }
}

bool OpenVINOImageInference::DoNeedImagePreProcessing(const InferenceBackend::ImagePtr image) {
    if (pre_processor.get() != nullptr)
        return true;

    if (image == nullptr)
        return false;

    // OV plugins exhibit low peformance (serialization) when VA pre-processor generates non-continous tensors
    // Add OpenCV pre-rocessor to compress non-continous tensors on top of VA pre-processor which does resize/crop/etc.
    if ((_impl->_memory_type == MemoryType::SYSTEM) &&
        ((image->format == FourCC::FOURCC_RGBP) || (image->format == FourCC::FOURCC_BGRP))) {

        if (!isImageContiguous(*image)) {
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

    const size_t batch_index = request->buffers.size();

    for (const auto &preprocessor : input_preprocessors) {
        if (preprocessor.second == nullptr)
            continue;

        if (preprocessor.first == KEY_image) {
            if (!DoNeedImagePreProcessing(nullptr))
                continue;
        }

        const auto &model_inputs = _impl->_model->inputs();
        std::shared_ptr<OpenvinoInputTensor> intput_tensor = nullptr;
        if ((preprocessor.first == KEY_image) && (model_inputs.size() == 1) && !request->in_tensors.empty() &&
            !request->in_tensors.front().empty()) {
            intput_tensor = std::make_shared<OpenvinoInputTensor>(request->in_tensors.front().front(), batch_index);
        } else if (model_inputs.size() == 1) {
            intput_tensor = std::make_shared<OpenvinoInputTensor>(request->infer_request.get_input_tensor());
        } else {
            intput_tensor =
                std::make_shared<OpenvinoInputTensor>(request->infer_request.get_tensor(preprocessor.second->name));
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

int OpenVINOImageInference::GetBatchTimeout() const {
    return batch_timeout;
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
    auto info = ModelApiConverters::get_model_info_postproc(_impl->_model, _impl->_model_path);
    return info;
}

std::map<std::string, GstStructure *> OpenVINOImageInference::GetModelInfoPreproc(const std::string model_file,
                                                                                  const gchar *pre_proc_config,
                                                                                  const gchar *ov_extension_lib) {
    if (ov_extension_lib && ov_extension_lib[0] != '\0') {
        openvino_core().add_extension(ov_extension_lib);
    }
    std::shared_ptr<ov::Model> model = openvino_core().read_model(model_file);

    auto info = ModelApiConverters::get_model_info_preproc(std::move(model), model_file, pre_proc_config);
    return info;
}

std::map<std::string, std::vector<size_t>> OpenVINOImageInference::GetModelInputShapes(const std::string model_file,
                                                                                       const gchar *ov_extension_lib) {
    if (ov_extension_lib && ov_extension_lib[0] != '\0') {
        openvino_core().add_extension(ov_extension_lib);
    }
    std::shared_ptr<ov::Model> model = openvino_core().read_model(model_file);

    std::map<std::string, std::vector<size_t>> res;
    for (const auto &input : model->inputs()) {
        const auto &partial_shape = input.get_partial_shape();
        const auto &shape = partial_shape.is_dynamic() ? partial_shape.get_min_shape() : partial_shape.get_shape();
        const std::string name = input.get_names().size() > 0 ? input.get_any_name() : std::string("input");
        res.emplace(name, std::vector<size_t>(shape.begin(), shape.end()));
    }
    return res;
}

bool OpenVINOImageInference::IsNpuStridedInputSupported() {
    try {
        const auto props = openvino_core().get_property("NPU", ov::supported_properties);
        return std::find(props.begin(), props.end(), ov::intel_npu::enable_strides_for.name()) != props.end();
    } catch (const std::exception &e) {
        GVA_WARNING("Could not query NPU supported properties: %s", e.what());
        return false;
    }
}

int OpenVINOImageInference::GetDeviceOptimalBatchSize(const std::string &device) {
    try {
        return static_cast<int>(openvino_core().get_property(device, ov::optimal_batch_size));
    } catch (...) {
        return 1; // Fallback if optimal batch size property is not supported
    }
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
                // WA: Fill non-complete batch with last element for bypass preprocessing only.
                // When software preprocessing is used, frames are written directly into the infer-request-owned
                // batched tensor, so any unfilled tail slots in the last partial batch already contain data from a
                // previous request reuse.
                if (batch_size > 1 && !DoNeedImagePreProcessing(nullptr)) {
                    size_t input_idx = 0;
                    for (auto &input_vec : request->in_tensors) {
                        for (int i = input_vec.size(); i < batch_size; i++)
                            input_vec.push_back(input_vec.back());
                        // FIXME: move?
                        request->infer_request.set_input_tensors(input_idx, input_vec);
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
        req->infer_request.set_callback([](std::exception_ptr) {});
    }
}

void OpenVINOImageInference::WorkingFunction(const std::shared_ptr<BatchRequest> &request) {
    assert(request);

    std::map<std::string, OutputBlob::Ptr> output_blobs;
    const auto &outputs = _impl->_compiled_model.outputs();
    for (size_t i = 0; i < outputs.size(); i++) {
        auto name = outputs[i].get_names().size() > 0 ? outputs[i].get_any_name() : std::string("output");
        output_blobs[name] = std::make_shared<OpenvinoOutputTensor>(request->infer_request.get_output_tensor(i));
    }
    callback(output_blobs, request->buffers);
}
