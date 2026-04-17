/*******************************************************************************
 * Copyright (C) 2020-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "pre_processor_info_parser.hpp"

#include "safe_arithmetic.hpp"

#include <cassert>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
std::vector<double> GValueArrayToVector(GValueArray *arr) {

    std::vector<double> vec;
    vec.reserve(arr->n_values);
    for (guint i = 0; i < arr->n_values; ++i) {
        vec.push_back(g_value_get_double(g_value_array_get_nth(arr, i)));
    }
    return vec;
}

void deleteGValueArr(GValueArray *arr) {
    if (arr) {
        g_value_array_free(arr);
        arr = nullptr;
    }
}
} // namespace

PreProcParamsParser::PreProcParamsParser(const GstStructure *params) : params(params) {
}

InferenceBackend::InputImageLayerDesc::Ptr PreProcParamsParser::parse() const {
    if (!params or !gst_structure_n_fields(params)) {
        return nullptr;
    }

    const auto resize = getResize();
    const auto crop = getCrop();

    const auto color_space = getColorSpace();
    const auto range_norm = getRangeNormalization();
    const auto distrib_norm = getDistribNormalization();

    const auto padding = getPadding();
    const auto resize_target_size = getResizeTargetSize();
    const auto resize_multiple = getResizeMultiple();

    return std::make_shared<InferenceBackend::InputImageLayerDesc>(
        InferenceBackend::InputImageLayerDesc(resize, crop, color_space, range_norm, distrib_norm, padding,
                                              resize_target_size.first, resize_target_size.second, resize_multiple));
}

PreProcResize PreProcParamsParser::getResize() const {
    PreProcResize resize_val = PreProcResize::NO;

    if (gst_structure_has_field(params, "resize")) {
        const gchar *_resize_type = gst_structure_get_string(params, "resize");
        if (!_resize_type) {
            throw std::runtime_error("\"resize\" string was broken.");
        }
        std::string resize_type(_resize_type);
        if (resize_type == "aspect-ratio") {
            resize_val = PreProcResize::ASPECT_RATIO;
        } else if (resize_type == "no-aspect-ratio") {
            resize_val = PreProcResize::NO_ASPECT_RATIO;
        } else if (resize_type == "aspect-ratio-pad") {
            resize_val = PreProcResize::ASPECT_RATIO_PAD;
        } else if (resize_type == "aspect-ratio-multiple") {
            resize_val = PreProcResize::ASPECT_RATIO_MULTIPLE;
        } else {
            throw std::runtime_error(std::string("Invalid type of resize: ") + resize_type);
        }
    }

    return resize_val;
}

PreProcCrop PreProcParamsParser::getCrop() const {
    PreProcCrop crop_val = PreProcCrop::NO;

    if (gst_structure_has_field(params, "crop")) {
        const gchar *_crop_type = gst_structure_get_string(params, "crop");
        if (!_crop_type) {
            throw std::runtime_error("\"crop\" string was broken.");
        }
        std::string crop_type(_crop_type);
        if (crop_type == "central") {
            crop_val = PreProcCrop::CENTRAL;
        } else if (crop_type == "central-resize") {
            crop_val = PreProcCrop::CENTRAL_RESIZE;
        } else if (crop_type == "top_left") {
            crop_val = PreProcCrop::TOP_LEFT;
        } else if (crop_type == "top_right") {
            crop_val = PreProcCrop::TOP_RIGHT;
        } else if (crop_type == "bottom_left") {
            crop_val = PreProcCrop::BOTTOM_LEFT;
        } else if (crop_type == "bottom_right") {
            crop_val = PreProcCrop::BOTTOM_RIGHT;
        } else {
            throw std::runtime_error(std::string("Invalid type of crop: ") + crop_type);
        }
    }
    return crop_val;
}

PreProcColorSpace PreProcParamsParser::getColorSpace() const {
    PreProcColorSpace color_space_val = PreProcColorSpace::NO;
    if (gst_structure_has_field(params, "color_space")) {
        const gchar *_color_space_type = gst_structure_get_string(params, "color_space");
        if (!_color_space_type) {
            throw std::runtime_error("\"color_space\" string was broken.");
        }
        std::string color_space_type(_color_space_type);
        if (color_space_type == "RGB") {
            color_space_val = PreProcColorSpace::RGB;
        } else if (color_space_type == "BGR") {
            color_space_val = PreProcColorSpace::BGR;
        } else if (color_space_type == "YUV") {
            color_space_val = PreProcColorSpace::YUV;
        } else if (color_space_type == "GRAYSCALE") {
            color_space_val = PreProcColorSpace::GRAYSCALE;
        } else {
            throw std::runtime_error(std::string("Invalid target color format: ") + color_space_type);
        }
    }
    return color_space_val;
}

PreProcRangeNormalization PreProcParamsParser::getRangeNormalization() const {
    GValueArray *arr = nullptr;
    try {
        if (gst_structure_has_field(params, "range")) {
            gst_structure_get_array(const_cast<GstStructure *>(params), "range", &arr);
            if (!arr or arr->n_values != 2)
                throw std::runtime_error("Invalid \"range\" array in model-proc file. It should only contain two "
                                         "values (minimum and maximum)");

            auto range = GValueArrayToVector(arr);
            deleteGValueArr(arr);
            return PreProcRangeNormalization(range[0], range[1]);
        }
    } catch (const std::exception &e) {
        deleteGValueArr(arr);
        std::throw_with_nested(e);
    }
    return PreProcRangeNormalization();
}

PreProcDistribNormalization PreProcParamsParser::getDistribNormalization() const {
    GValueArray *arr = nullptr;
    try {
        if (gst_structure_has_field(params, "mean") and gst_structure_has_field(params, "std")) {
            std::vector<double> std, mean;

            gst_structure_get_array(const_cast<GstStructure *>(params), "mean", &arr);
            if (!arr or !arr->n_values)
                throw std::runtime_error("\"mean\" array is null.");

            mean = GValueArrayToVector(arr);
            deleteGValueArr(arr);

            gst_structure_get_array(const_cast<GstStructure *>(params), "std", &arr);
            if (!arr or !arr->n_values)
                throw std::runtime_error("\"std\" array is null.");

            std = GValueArrayToVector(arr);
            g_value_array_free(arr);
            arr = nullptr;

            return PreProcDistribNormalization(mean, std);
        }
    } catch (const std::exception &e) {
        deleteGValueArr(arr);
        std::throw_with_nested(e);
    }
    return PreProcDistribNormalization();
}

PreProcPadding PreProcParamsParser::getPadding() const {
    try {
        if (not gst_structure_has_field(params, "padding"))
            return PreProcPadding{};

        const GValue *value = gst_structure_get_value(params, "padding");
        assert(value != nullptr && "padding Gvalue from model-proc is nullptr.");

        const GstStructure *padding_s = gst_value_get_structure(value);
        assert(padding_s != nullptr && "GstStructure padding field from gvalue is nullptr.");

        if (gst_structure_has_field(padding_s, "stride") and
            (gst_structure_has_field(padding_s, "stride_x") or gst_structure_has_field(padding_s, "stride_y")))
            throw std::runtime_error("Padding structure has exta information about stride.");

        size_t stride_x = 0;
        size_t stride_y = 0;
        std::vector<double> fill_value(3, 0);
        int val = -1;

        if (gst_structure_has_field(padding_s, "fill_value")) {
            GValueArray *arr = nullptr;
            gst_structure_get_array(const_cast<GstStructure *>(padding_s), "fill_value", &arr);
            if (!arr or !arr->n_values)
                throw std::runtime_error("\"fill_value\" array is null.");

            fill_value = GValueArrayToVector(arr);
            deleteGValueArr(arr);
        }

        if (gst_structure_has_field(padding_s, "stride")) {
            gst_structure_get_int(padding_s, "stride", &val);
            size_t stride = safe_convert<size_t>(val);
            stride_x = stride;
            stride_y = stride;
        } else {
            if (gst_structure_has_field(padding_s, "stride_x")) {
                gst_structure_get_int(padding_s, "stride_x", &val);
                stride_x = safe_convert<size_t>(val);
            }

            if (gst_structure_has_field(padding_s, "stride_y")) {
                gst_structure_get_int(padding_s, "stride_y", &val);
                stride_y = safe_convert<size_t>(val);
            }
        }

        return PreProcPadding(stride_x, stride_y, fill_value);

    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Error during \"padding\" structure parse."));
    }

    return PreProcPadding{};
}

std::pair<size_t, size_t> PreProcParamsParser::getResizeTargetSize() const {
    const GValue *garray = gst_structure_get_value(params, "reshape_size");
    if (!garray || gst_value_array_get_size(garray) != 2) {
        return {0, 0};
    }

    const GValue *height = gst_value_array_get_value(garray, 0);
    const GValue *width = gst_value_array_get_value(garray, 1);
    if (!height || !width) {
        throw std::runtime_error("Invalid reshape_size array in model-proc");
    }

    return {safe_convert<size_t>(g_value_get_int(width)), safe_convert<size_t>(g_value_get_int(height))};
}

size_t PreProcParamsParser::getResizeMultiple() const {
    int resize_multiple = 1;
    if (gst_structure_get_int(params, "resize-multiple", &resize_multiple)) {
        if (resize_multiple <= 0) {
            throw std::runtime_error("resize-multiple must be greater than zero");
        }
        return safe_convert<size_t>(resize_multiple);
    }

    return 1;
}
