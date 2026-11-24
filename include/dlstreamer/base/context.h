/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/context.h"
#include "dlstreamer/memory_mapper.h"
#include <map>
#include <vector>

namespace dlstreamer {

class BaseContext : public Context {
  public:
    struct key {
        static constexpr auto va_display = "va_display";     // (VAAPI) VADisplay
        static constexpr auto va_tile_id = "va_tile_id";     // (VAAPI) VADisplay
        static constexpr auto cl_context = "cl_context";     // (OpenCL) cl_context
        static constexpr auto cl_queue = "cl_queue";         // (OpenCL) cl_command_queue
        static constexpr auto ze_context = "ze_context";     // (Level-zero) ze_context_handle_t
        static constexpr auto ze_device = "ze_device";       // (Level-zero) ze_device_handle_t
        static constexpr auto d3d11_device = "d3d11_device"; // (D3D11) GstD3D11Device
        static constexpr auto d3d12_device = "d3d12_device"; // (D3D12) GstD3D12Device
    };

    BaseContext(MemoryType memory_type) : _memory_type(memory_type) {
    }

    MemoryType memory_type() const override {
        return _memory_type;
    }

    handle_t handle(std::string_view /*key*/) const noexcept override {
        return nullptr;
    }

    virtual std::vector<std::string> keys() const {
        return {};
    }

    MemoryMapperPtr get_mapper(const ContextPtr &input_context, const ContextPtr &output_context) override {
        auto mapper_it = _mappers.find({input_context.get(), output_context.get()});
        return (mapper_it != _mappers.end()) ? mapper_it->second : nullptr;
    }

    ContextPtr derive_context(MemoryType /*memory_type*/) noexcept override {
        return nullptr;
    }

    ContextPtr parent() noexcept override {
        return _parent;
    }

    void set_parent(ContextPtr parent) {
        _parent = parent;
    }

    void set_memory_type(MemoryType memory_type) {
        _memory_type = memory_type;
    }

    void attach_mapper(MemoryMapperPtr mapper) {
        if (mapper)
            _mappers[{mapper->input_context().get(), mapper->output_context().get()}] = mapper;
    }

    void remove_mapper(MemoryMapperPtr mapper) {
        _mappers.erase({mapper->input_context().get(), mapper->output_context().get()});
    }

    ~BaseContext() {
        for (auto &mapper : _mappers) {
            BaseContext *input_context = dynamic_cast<BaseContext *>(mapper.first.first);
            if (input_context && input_context != this)
                input_context->remove_mapper(mapper.second);
            BaseContext *output_context = dynamic_cast<BaseContext *>(mapper.first.second);
            if (output_context && output_context != this)
                output_context->remove_mapper(mapper.second);
        }
    }

  protected:
    MemoryType _memory_type;
    ContextPtr _parent;
    std::map<std::pair<Context *, Context *>, MemoryMapperPtr> _mappers; // use pointers (not shared_ptr) by perf reason

    template <typename T>
    static inline std::shared_ptr<T> create_from_another(const ContextPtr &another_context,
                                                         MemoryType new_memory_type) {
        auto casted = std::dynamic_pointer_cast<T>(another_context);
        if (casted)
            return casted;
        ContextPtr ctx = another_context;
        if (another_context && another_context->memory_type() != new_memory_type) {
            auto derived = another_context->derive_context(new_memory_type);
            if (derived) {
                casted = std::dynamic_pointer_cast<T>(derived);
                if (casted)
                    return casted;
                ctx = derived;
            }
        }
        return std::make_shared<T>(ctx);
    }
};

} // namespace dlstreamer
