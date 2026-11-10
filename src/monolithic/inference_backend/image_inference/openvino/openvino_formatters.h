/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// fmt::formatter specializations used when logging in the OpenVINO backend:
// ImagePreprocessorType (readable name), std::exception_ptr (nested messages), ov::AnyMap entries (key: value).

#pragma once

#include "inference_backend/pre_proc.h"

#include <spdlog/fmt/fmt.h>

#include <openvino/core/any.hpp>

#include <exception>
#include <string>

// Prints a readable pre-processor name (e.g. "VAAPI System Memory") instead of an enum number.
// Used in the "Pre-processing: ..." and "creating pre-processor, type: ..." log lines.
// TODO: the name table could live next to the enum in pre_proc.h if other code needs it.
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
        case ImagePreprocessorType::D3D12:
            name = "D3D12 System Memory";
            break;
        case ImagePreprocessorType::D3D12_SURFACE_SHARING:
            name = "D3D12 Surface Sharing";
            break;
        }
        return formatter<string_view>::format(name, ctx);
    }
};

// Prints the message of an exception including nested ones, up to max_level deep.
// Used for the error that OpenVINO reports to the infer request completion callback.
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

// Prints one "key: value" entry of an ov::AnyMap, so fmt::join can list the properties passed to compile_model.
template <>
struct fmt::formatter<ov::AnyMap::value_type> {
    constexpr auto parse(format_parse_context &ctx) {
        return ctx.begin();
    }

    auto format(const ov::AnyMap::value_type &pair, format_context &ctx) const {
        return fmt::format_to(ctx.out(), "{}: {}", pair.first, pair.second.as<std::string>());
    }
};
