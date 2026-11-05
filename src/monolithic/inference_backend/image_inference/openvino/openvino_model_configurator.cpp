/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// ModelConfigurator: the order of the configuration steps matters (reshape, then OpenVINO pre-processing, then
// batch size), see configure(). The layout comments in configure_image_input explain how layouts are reconciled.

#include "openvino_model_configurator.h"

#include "openvino_image_inference.h"

#include "inference_backend/logger.h"

#include <openvino/runtime/intel_gpu/properties.hpp>

#include <spdlog/fmt/bundled/ranges.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include "openvino_formatters.h"

namespace InferenceBackend {
namespace OpenVino {

namespace {

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

#ifndef ENABLE_D3D_NPU_COLOR_CONV
ModelConfigurator::ModelConfigurator(std::shared_ptr<ov::Model> model, MemoryType memory_type)
    : _model(std::move(model)), _memory_type(memory_type) {
#else
ModelConfigurator::ModelConfigurator(std::shared_ptr<ov::Model> model, MemoryType memory_type,
                                     ImagePreprocessorType pp_type)
    : _model(std::move(model)), _memory_type(memory_type), _pp_type(pp_type) {
#endif
    size_t bs;
    int fmt, mt;
    get_model_image_input_info(_origin_model_in_w, _origin_model_in_h, bs, fmt, mt);
}

void ModelConfigurator::get_model_image_input_info(size_t &width, size_t &height, size_t &batch_size, int &format,
                                                   int &memory_type) const {
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

void ModelConfigurator::configure(const ConfigHelper &config) {
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

void ModelConfigurator::configure_model_inputs(const ConfigHelper &config, ov::preprocess::PrePostProcessor &preproc) {
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

ov::Layout ModelConfigurator::get_image_layout_from_shape(const ov::PartialShape &shape) {
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

ov::Layout ModelConfigurator::get_ov_node_layout(const ov::Output<ov::Node> &node, bool from_shape_fallback) const {
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
        GVA_WARNING("Node '%s': couldn't determine layout for shape %s", node_name.c_str(), pshape.to_string().c_str());
    else
        GVA_INFO("Node '%s': got layout %s for shape %s", node_name.c_str(), result.to_string().c_str(),
                 pshape.to_string().c_str());
    return result;
}

void ModelConfigurator::configure_image_input(const ConfigHelper &config, ov::preprocess::InputInfo &input,
                                              const ConfigHelper::InputConfig &input_config,
                                              const ov::Output<ov::Node> &node) {
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
    // ppp.build() (in configure) reconciles the two: the resulting Parameter ends up
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
        GVA_WARNING("Layout for '%s' input is not explicitly set, so it's defaulted to %s", node.get_any_name().c_str(),
                    model_layout.to_string().c_str());
        input.model().set_layout(model_layout);
    }
}

size_t ModelConfigurator::get_model_channels_num(const ConfigHelper &config) {
    const auto &model_format = config.model_format();
    if (model_format == "GRAYSCALE")
        return 1;
    return 3;
}

bool ModelConfigurator::is_image_input(const ConfigHelper &config, const ov::Output<ov::Node> &input) const {
    const auto inputs_cfg = config.inputs_cfg();
    const auto cmp_pred = [&](const auto &pair) { return input.get_names().count(pair.first) != 0; };
    auto it = std::find_if(inputs_cfg.begin(), inputs_cfg.end(), cmp_pred);
    if (it != inputs_cfg.end())
        return it->second.data_format == InferenceBackend::KEY_image;

    return _model->inputs().size() == 1;
}

void ModelConfigurator::reshape_model(const ConfigHelper &config, size_t image_height, size_t image_width,
                                      bool reshape_static) {
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

std::pair<ov::preprocess::ColorFormat, std::vector<std::string>>
ModelConfigurator::get_ov_color_format(const std::string &img_format) {
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

} // namespace OpenVino
} // namespace InferenceBackend
