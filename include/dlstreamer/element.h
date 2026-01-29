/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/dictionary.h"
#include "dlstreamer/context.h"
#include "dlstreamer/frame_info.h"
#include <functional>

namespace dlstreamer {

/**
 * @brief Base class for all elements (Source, Transform, TransformInplace and Sink)
 *
 * The caller is responsible for thread safety.
 */
class Element {
  public:
    virtual ~Element() {
    }

    /**
     * @brief Initialize element according to input/output information. Function returns false in initialization failed.
     */
    virtual bool init() = 0;

    /**
     * @brief The function requests element to create context of specified memory type (or return existent one).
     * If context can't be created, function returns nullptr.
     * @param memory_type Memory type of requested context
     */
    virtual ContextPtr get_context(MemoryType memory_type) noexcept = 0;
};

using ElementPtr = std::shared_ptr<Element>;

namespace param {
static constexpr auto logger_name = "logger_name";
}

/**
 * @brief Structure describing element parameter - name, short description, default value, range or list (for strings)
 * of supported values.
 */
struct ParamDesc {
    std::string name;
    std::string description;
    Any default_value;
    std::vector<Any> range;

    ParamDesc(std::string_view name, std::string_view desc, Any default_value, std::vector<Any> valid_values = {})
        : name(name), description(desc), default_value(std::move(default_value)), range(std::move(valid_values)) {
    }

    ParamDesc(std::string_view name, std::string_view desc, const Any &default_value, const Any &min_value,
              const Any &max_value)
        : ParamDesc(name, desc, default_value, {min_value, max_value}) {
    }

    // TODO can we use general constructor for string-typed parameters?
    ParamDesc(std::string_view name, std::string_view desc, const char *default_value,
              std::vector<std::string> valid_values = {})
        : name(name), description(desc), default_value(std::string(default_value)) {
        if (!valid_values.empty())
            range = std::vector<Any>(valid_values.begin(), valid_values.end());
    }

    template <typename T>
    bool is_type() const {
        return any_holds_type<T>(default_value);
    }
};

using ParamDescVector = std::vector<ParamDesc>;

enum ElementFlags {
    ELEMENT_FLAG_EXTERNAL_MEMORY = (1 << 0), // internal allocation not supported
    ELEMENT_FLAG_SHARABLE = (1 << 1),
};

static constexpr int32_t ElementDescMagic = 0x34495239;

#define MAKE_FRAME_INFO_VECTOR(...)                                                                                    \
    []() -> const FrameInfoVector & {                                                                                  \
        static const FrameInfoVector info{__VA_ARGS__};                                                                \
        return info;                                                                                                   \
    }

/**
 * @brief The structure used to register element and create element instance.
 */
struct ElementDesc {
    int32_t magic = ElementDescMagic;
    std::string_view name;
    std::string_view description;
    std::string_view author;
    ParamDescVector *params;
    std::function<const FrameInfoVector &()> input_info;
    std::function<const FrameInfoVector &()> output_info;
    const std::function<Element *(DictionaryCPtr params, const ContextPtr &app_context)> create;
    int flags = 0;
};

template <class Ty>
static Element *create_element(DictionaryCPtr params, const ContextPtr &app_context) {
    return new Ty(params, app_context);
}

} // namespace dlstreamer

#if _WIN32
#define DLS_EXPORT __declspec(dllexport)
#else
#define DLS_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {
extern DLS_EXPORT const dlstreamer::ElementDesc *dlstreamer_elements[];
}
