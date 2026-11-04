/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/context.h"
#include "dlstreamer/d3d11/tensor.h"
#include "dlstreamer/utils.h"
#include <d3d11.h>
#include <gst/d3d11/gstd3d11device.h>

namespace dlstreamer {

class D3D11Context;
using D3D11ContextPtr = std::shared_ptr<D3D11Context>;

class D3D11Context : public BaseContext {
  public:
    struct key {
        static constexpr auto d3d11_device = BaseContext::key::d3d11_device;
    };

    static inline D3D11ContextPtr create(const ContextPtr &another_context) {
        // FIXME: Add support for VA only
        return create_from_another<D3D11Context>(another_context, MemoryType::D3D11);
    }

    D3D11Context(void *d3d_device) : BaseContext(MemoryType::D3D11) {
        _d3d_device = static_cast<GstD3D11Device *>(d3d_device);
    }

    D3D11Context(const ContextPtr &another_context) : BaseContext(MemoryType::D3D11) {
        DLS_CHECK(another_context);
        DLS_CHECK(_d3d_device = static_cast<GstD3D11Device *>(another_context->handle(key::d3d11_device)));
        _parent = another_context;
    }

    GstD3D11Device *d3d_device() {
        return _d3d_device;
    }

    std::vector<std::string> keys() const override {
        return {key::d3d11_device};
    }

    void *handle(std::string_view key) const noexcept override {
        if (key == key::d3d11_device || key.empty())
            return _d3d_device;
        return nullptr;
    }

    MemoryMapperPtr get_mapper(const ContextPtr &input_context, const ContextPtr &output_context) override {
        auto mapper = BaseContext::get_mapper(input_context, output_context);
        if (mapper)
            return mapper;

        return nullptr;
    }

  protected:
    GstD3D11Device *_d3d_device = nullptr;
};

} // namespace dlstreamer
