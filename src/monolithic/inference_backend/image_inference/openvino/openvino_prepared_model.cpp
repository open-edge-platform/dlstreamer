/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// PrepareModel(): the steps depend on each other, see the order in PrepareModel(). The layout comments in
// ConfigureImageInput explain how tensor and model layouts are reconciled.

#include "openvino_prepared_model.h"

#include "openvino_formatters.h"
#include "openvino_image_inference.h"

#include "inference_backend/logger.h"

#include <openvino/runtime/intel_gpu/properties.hpp>

#include <spdlog/fmt/bundled/ranges.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cassert>
#include <sstream>
#include <stdexcept>

namespace InferenceBackend {
namespace OpenVino {

namespace {

// What the preparation steps learn about the model and hand to the later steps.
struct Preparation {
    std::shared_ptr<ov::Model> model;
    MemoryType memory_type;
    ImagePreprocessorType pp_type = ImagePreprocessorType::AUTO;
    std::string model_format;
    std::string image_input_name;
    int batch_size = 0;
    int batch_timeout = -1;
    int auto_batch_num_requests = 0;

    // Input size of the model as it was read, before any reshape or resize.
    size_t origin_model_in_w = 0;
    size_t origin_model_in_h = 0;
    // True when OpenVINO pre-processing was set up to resize the image input (IE pre-processor only).
    bool resize_applied = false;
};

void PrintInputAndOutputsInfo(const ov::Model &network) {
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

ov::Layout GetImageLayoutFromShape(const ov::PartialShape &shape) {
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
ov::Layout GetOvNodeLayout(const ov::Output<ov::Node> &node, bool from_shape_fallback = false) {
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
    result = GetImageLayoutFromShape(pshape);
    if (result.empty())
        GVA_WARNING("Node '%s': couldn't determine layout for shape %s", node_name.c_str(), pshape.to_string().c_str());
    else
        GVA_INFO("Node '%s': got layout %s for shape %s", node_name.c_str(), result.to_string().c_str(),
                 pshape.to_string().c_str());
    return result;
}

std::pair<ov::preprocess::ColorFormat, std::vector<std::string>> GetOvColorFormat(const std::string &img_format) {
    using namespace ov::preprocess;
    static const std::map<std::string, std::pair<ov::preprocess::ColorFormat, std::vector<std::string>>> FORMAT_MAP{
        {"NV12", {ColorFormat::NV12_TWO_PLANES, {"y", "uv"}}},
        {"I420", {ColorFormat::I420_THREE_PLANES, {"y", "u", "v"}}},
        {"RGB", {ColorFormat::RGB, {}}},
        {"BGR", {ColorFormat::BGR, {}}},
        {"RGBX", {ColorFormat::RGBX, {}}},
        {"BGRX", {ColorFormat::BGRX, {}}},
        {"RGBA", {ColorFormat::RGBX, {}}},
        {"BGRA", {ColorFormat::BGRX, {}}}};
    auto iter = FORMAT_MAP.find(img_format);
    if (iter != FORMAT_MAP.end()) {
        return iter->second;
    }

    GVA_ERROR("Color format '%s' is not supported by Inference Engine preprocessing. UNDEFINED will be set",
              img_format.c_str());
    return {ov::preprocess::ColorFormat::UNDEFINED, {}};
}

size_t GetModelChannelsNum(const ConfigHelper &config) {
    const auto &model_format = config.ModelFormat();
    if (model_format == "GRAYSCALE")
        return 1;
    return 3;
}

bool IsImageInput(const Preparation &p, const ConfigHelper &config, const ov::Output<ov::Node> &input) {
    const auto inputs_cfg = config.InputsCfg();
    const auto cmp_pred = [&](const auto &pair) { return input.get_names().count(pair.first) != 0; };
    auto it = std::find_if(inputs_cfg.begin(), inputs_cfg.end(), cmp_pred);
    if (it != inputs_cfg.end())
        return it->second.data_format == InferenceBackend::KEY_image;

    return p.model->inputs().size() == 1;
}

// Size, batch, pixel format and memory type of the image input, from the state the preparation has reached.
ImageInputInfo DescribeImageInput(const Preparation &p) {
    ImageInputInfo info;

    for (auto input : p.model->inputs()) {
        // FIXME: strange dependency on input image name
        if (!p.image_input_name.empty() && !input.get_names().count(p.image_input_name))
            continue;

        const auto &partial_shape = input.get_partial_shape();
        const auto &shape = partial_shape.is_dynamic() ? partial_shape.get_min_shape() : partial_shape.get_shape();
        const auto layout = GetOvNodeLayout(input, true);
        GVA_DEBUG("DescribeImageInput(): input: %s, shape: %s, partial shape: %s, layout: %s",
                  input.get_any_name().c_str(), shape.to_string().c_str(), partial_shape.to_string().c_str(),
                  layout.to_string().c_str());

        if (ov::layout::has_batch(layout)) {
            const std::int64_t batch_idx = ov::layout::batch_idx(layout);
            info.batch_size = shape[batch_idx];
        }

        if (ov::layout::has_width(layout)) {
            const std::int64_t width_idx = ov::layout::width_idx(layout);
            info.width = shape[width_idx];
        }

        if (ov::layout::has_height(layout)) {
            const std::int64_t height_idx = ov::layout::height_idx(layout);
            info.height = shape[height_idx];
        }

        break;
    }

    if (info.width == 0 && info.height == 0) {
        info.width = p.origin_model_in_w;
        info.height = p.origin_model_in_h;
        GVA_DEBUG("DescribeImageInput(): using wa, w=%zu, h=%zu", info.width, info.height);
    }

    if (info.batch_size == 0) {
        info.batch_size = p.batch_size;
    }

    switch (p.memory_type) {
    case MemoryType::SYSTEM:
    case MemoryType::DMA_BUFFER:
        if (p.model_format == "BGR")
            info.format = FourCC::FOURCC_BGRP;
        else
            info.format = FourCC::FOURCC_RGBP;
        break;
    case MemoryType::VAAPI:
        info.format = FourCC::FOURCC_NV12;
        break;
    case MemoryType::D3D11:
    case MemoryType::D3D12:
#ifdef _WIN32
        // D3D11/D3D12 surface sharing wraps a BGRA/RGBA texture as a single OpenCL
        // buffer, so the texture format must match the model's channel order.
        if (p.pp_type == ImagePreprocessorType::D3D11_SURFACE_SHARING ||
            p.pp_type == ImagePreprocessorType::D3D12_SURFACE_SHARING) {
            info.format = p.model_format == "BGR" ? FourCC::FOURCC_BGRA : FourCC::FOURCC_RGBA;
            break;
        }
#endif
        info.format = FourCC::FOURCC_NV12;
        break;
    default:
        throw std::runtime_error("Unsupported memory type");
    }
    info.memory_type = static_cast<int>(p.memory_type);

    if (p.resize_applied) {
        // Set original model width and height values if the resizing was done.
        // Applicable for ImagePreprocessorType::IE only.
        info.width = p.origin_model_in_w;
        info.height = p.origin_model_in_h;
    }
    return info;
}

void ReshapeModel(Preparation &p, const ConfigHelper &config, size_t image_height, size_t image_width,
                  bool reshape_static) {
    std::map<ov::Output<ov::Node>, ov::PartialShape> port_to_shape;

    for (const ov::Output<ov::Node> &input : p.model->inputs()) {
        ov::PartialShape shape = input.get_partial_shape();

        const ov::Layout layout = GetOvNodeLayout(input, true);
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

        if (IsImageInput(p, config, input)) {
            const auto channels_idx = ov::layout::channels_idx(layout);
            if (channels_idx >= 0 && shape[channels_idx].is_dynamic())
                shape[channels_idx] = GetModelChannelsNum(config);
        }

        GVA_INFO("Reshaping model input %s from %s to %s", input.get_any_name().c_str(),
                 input.get_partial_shape().to_string().c_str(), shape.to_string().c_str());
        port_to_shape[input] = shape;
    }
    p.model->reshape(port_to_shape);
    PrintInputAndOutputsInfo(*p.model);
}

void ConfigureImageInput(Preparation &p, const ConfigHelper &config, ov::preprocess::InputInfo &input,
                         const ConfigHelper::InputConfig &input_config, const ov::Output<ov::Node> &node) {
    using InferenceBackend::ImagePreprocessorType;
    const auto pp_type = config.PpType();
    std::string pp_type_string = fmt::format("Pre-processing: {}", pp_type);
    GVA_DEBUG("%s", pp_type_string.c_str());

    // OPENCV and VAAPI pre-processors handle color conversion and scaling, input tensors in NCHW format
    if (pp_type == ImagePreprocessorType::OPENCV || pp_type == ImagePreprocessorType::VAAPI_SYSTEM ||
        pp_type == ImagePreprocessorType::D3D11 || pp_type == ImagePreprocessorType::D3D12) {
        input.tensor().set_layout("NCHW");
    }

    // OV preprocessing is configured for IE, VAAPI_SURFACE_SHARING, D3D11_SURFACE_SHARING or D3D12_SURFACE_SHARING
    if (pp_type == ImagePreprocessorType::VAAPI_SURFACE_SHARING ||
        pp_type == ImagePreprocessorType::D3D11_SURFACE_SHARING ||
        pp_type == ImagePreprocessorType::D3D12_SURFACE_SHARING || pp_type == ImagePreprocessorType::IE) {

        // Surface sharing and IE assume input tensors in NHWC format
        const auto color_format_pair = GetOvColorFormat(config.ImageFormat());
        input.tensor().set_color_format(color_format_pair.first, color_format_pair.second);
        input.tensor().set_layout("NHWC");

        // Surface sharing pre-processor requires NV12 input surface in GPU memory
        if (pp_type == ImagePreprocessorType::VAAPI_SURFACE_SHARING) {
            assert(p.memory_type == MemoryType::VAAPI);
            assert(color_format_pair.first == ov::preprocess::ColorFormat::NV12_TWO_PLANES);
            input.tensor().set_memory_type(ov::intel_gpu::memory_type::surface);
        }
        if (pp_type == ImagePreprocessorType::D3D11_SURFACE_SHARING) {
            assert(p.memory_type == MemoryType::D3D11);
            // BGRA/RGBA via OpenCL interop feeds an NHWC u8 CL buffer tensor and does not use
            // memory_type::surface. Only the NV12 path uses surface tagging.
            if (color_format_pair.first == ov::preprocess::ColorFormat::NV12_TWO_PLANES)
                input.tensor().set_memory_type(ov::intel_gpu::memory_type::surface);
        }
        if (pp_type == ImagePreprocessorType::D3D12_SURFACE_SHARING) {
            assert(p.memory_type == MemoryType::D3D12);
            // BGRA via cl_khr_external_memory buffer import, plain NHWC u8 buffer tensor.
            assert(color_format_pair.first != ov::preprocess::ColorFormat::NV12_TWO_PLANES);
        }

        // Convert input tensor color space into model color space if necessary
        const auto model_format_pair = GetOvColorFormat(config.ModelFormat());
        if (color_format_pair.first != model_format_pair.first)
            input.preprocess().convert_color(model_format_pair.first);

        // Check if image resize is required (IE-only, VAAPI_SURFACE_SHARING does image resize)
        if (pp_type == ImagePreprocessorType::IE) {
            // System memory input
            assert(p.memory_type == MemoryType::SYSTEM);
            // Validate resize_applied flag. We should not resize a model twice.
            assert(p.resize_applied == false);

            auto [img_width, img_height] = config.ImageSize();
            bool apply_resize = img_width != p.origin_model_in_w || img_height != p.origin_model_in_h;

            if (img_width && img_height) {
                input.tensor().set_spatial_static_shape(img_height, img_width);
            } else {
                // Dynamic input, e.g. ROI
                input.tensor().set_spatial_dynamic_shape();
            }

            if (apply_resize) {
                input.preprocess().resize(ov::preprocess::ResizeAlgorithm::RESIZE_LINEAR);
                p.resize_applied = true;
            }
        }
    }

    // Input tensors are u8, convert to model precision if necessary
    auto node_element_type = node.get_element_type();
    if (node_element_type != input_config.type)
        input.preprocess().convert_element_type(node_element_type);

    // If defined, scale the image by the defined value
    auto mean = config.PixelValueMean();
    auto scale = config.PixelValueScale();

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
    // ppp.build() (in PrepareModel) reconciles the two: the resulting Parameter ends up
    // carrying the TENSOR layout, and that is what ov::set_batch later reads. Since both NCHW
    // and NHWC put 'N' at axis 0, set_batch always finds the batch on the image input.
    // The "??HW" hint below is transient: only H/W placement matters for the model side, so we
    // leave N/C unspecified. It does not become the Parameter's final layout.
    // NOTE: this whole function only runs for the image input (data_format == KEY_image); aux
    // inputs (e.g. calib/img_sizes) get their batch ('N') layout tagged in ConfigureModelInputs
    // instead, so ov::set_batch can pin their batch axis too.
    ov::Layout model_layout = GetOvNodeLayout(node);
    if (model_layout.empty()) {
        // Need to specify H and W dimensions in model, others are not important
        model_layout = "??HW";
        GVA_WARNING("Layout for '%s' input is not explicitly set, so it's defaulted to %s", node.get_any_name().c_str(),
                    model_layout.to_string().c_str());
        input.model().set_layout(model_layout);
    }
}

void ConfigureModelInputs(Preparation &p, const ConfigHelper &config, ov::preprocess::PrePostProcessor &preproc) {
    const auto &inputs = p.model->inputs();

    const ConfigHelper::InputsConfig inputs_cfg = config.InputsCfg();
    PrintInputsConfig(inputs_cfg);

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
            p.image_input_name = item.get_any_name();
            GVA_DEBUG("Found image input: %s, layout: %s", p.image_input_name.c_str(),
                      GetOvNodeLayout(item).to_string().c_str());
            ConfigureImageInput(p, config, in, in_cfg, item);
        } else {
            // Auxiliary (non-image) input (e.g. MonoDETR's calib/img_sizes): tag a batch ('N')
            // layout so the later ov::set_batch can pin its batch axis. Only when axis 0 is dynamic
            // and no layout is set, to avoid rewriting a legitimately fixed leading dimension.
            const auto &pshape = item.get_partial_shape();
            if (GetOvNodeLayout(item).empty() && pshape.rank().is_static() && pshape.size() > 0 &&
                pshape[0].is_dynamic())
                in.model().set_layout(ov::Layout("N" + std::string(pshape.size() - 1, '?')));
        }
    }

    std::stringstream ppp_ss;
    ppp_ss << preproc;
    GVA_DEBUG("%s", ppp_ss.str().c_str());
}

} // namespace

BatchSettings ResolveBatchSettings(const ConfigHelper &config) {
    BatchSettings settings;
    settings.batch_size = config.BatchSize();
    if (settings.batch_size == 0)
        settings.batch_size = OpenVinoImageInference::GetDeviceOptimalBatchSize(config.Device());

    if (config.BatchTimeout() > -1) {
        settings.auto_batch_num_requests = settings.batch_size;
        settings.batch_size = 1;
    }
    return settings;
}

PreparedModel PrepareModel(std::shared_ptr<ov::Model> model, const ConfigHelper &config, MemoryType memory_type) {
    Preparation p;
    p.model = std::move(model);
    p.memory_type = memory_type;
    p.pp_type = config.PpType();
    p.model_format = config.ModelFormat();

    // The pre-processing configured below compares the frame size with the size the model was read with.
    const ImageInputInfo original_input = DescribeImageInput(p);
    p.origin_model_in_w = original_input.width;
    p.origin_model_in_h = original_input.height;

    auto [reshape_width, reshape_height] = config.ReshapeSize();
    if (config.NeedReshape() && (reshape_width || reshape_height))
        ReshapeModel(p, config, reshape_height, reshape_width, config.ReshapeStatic());

    auto ppp = ov::preprocess::PrePostProcessor(p.model);
    ConfigureModelInputs(p, config, ppp);
    p.model = ppp.build();

    // dynamic shapes may have height=0 and width=0 after IE preprocessing
    // set lower and upper bound of dynamic shapes to match original frame dimensions
    auto [img_width, img_height] = config.ImageSize();
    if (img_width == 0 && img_height == 0 && config.PpType() == ImagePreprocessorType::IE) {
        auto [frame_width, frame_height] = config.FrameSize();
        ReshapeModel(p, config, frame_height, frame_width, false);
    }

    const BatchSettings batch = ResolveBatchSettings(config);
    p.batch_size = batch.batch_size;
    p.auto_batch_num_requests = batch.auto_batch_num_requests;
    p.batch_timeout = config.BatchTimeout();

    GVA_DEBUG("Setting batch size of %d to model", p.batch_size);
    // Every input carries an 'N' layout by now (image via ConfigureImageInput, aux inputs tagged
    // in ConfigureModelInputs), so set_batch pins the batch axis on all of them.
    ov::set_batch(p.model, p.batch_size);

    GVA_DEBUG("Model inputs after configuration:");
    size_t idx = 0;
    for (auto &input : p.model->inputs()) {
        std::string debug_string = fmt::format("  [{}]: {}, shape: {}", idx++, fmt::join(input.get_names(), " "),
                                               input.get_partial_shape().to_string());
        GVA_DEBUG("%s", debug_string.c_str());
    }

    // FIXME:
    auto item = p.model->inputs().front();
    p.image_input_name = item.get_any_name();

    PreparedModel prepared;
    prepared.model = p.model;
    prepared.image_input_name = p.image_input_name;
    prepared.batch_size = p.batch_size;
    prepared.batch_timeout = p.batch_timeout;
    prepared.auto_batch_num_requests = p.auto_batch_num_requests;
    prepared.image_input = DescribeImageInput(p);
    return prepared;
}

} // namespace OpenVino
} // namespace InferenceBackend
