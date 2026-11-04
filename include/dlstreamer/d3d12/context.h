/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/context.h"
#include "dlstreamer/d3d12/tensor.h"
#include "dlstreamer/utils.h"
#include <d3d12.h>
#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/d3d12/gstd3d12device.h>

namespace dlstreamer {

class D3D12Context;
using D3D12ContextPtr = std::shared_ptr<D3D12Context>;

class D3D12Context : public BaseContext {
  public:
    struct key {
        static constexpr auto d3d12_device = BaseContext::key::d3d12_device;
    };

    static inline D3D12ContextPtr create(const ContextPtr &another_context) {
        return create_from_another<D3D12Context>(another_context, MemoryType::D3D12);
    }

    D3D12Context(void *d3d12_device) : BaseContext(MemoryType::D3D12) {
        _d3d12_device = static_cast<GstD3D12Device *>(d3d12_device);
    }

    D3D12Context(const ContextPtr &another_context) : BaseContext(MemoryType::D3D12) {
        DLS_CHECK(another_context);
        DLS_CHECK(_d3d12_device = static_cast<GstD3D12Device *>(another_context->handle(key::d3d12_device)));
        _parent = another_context;
    }

    GstD3D12Device *d3d12_device() {
        return _d3d12_device;
    }

    std::vector<std::string> keys() const override {
        return {key::d3d12_device};
    }

    void *handle(std::string_view key) const noexcept override {
        if (key == key::d3d12_device || key.empty())
            return _d3d12_device;
        return nullptr;
    }

    MemoryMapperPtr get_mapper(const ContextPtr &input_context, const ContextPtr &output_context) override {
        return BaseContext::get_mapper(input_context, output_context);
    }

  protected:
    GstD3D12Device *_d3d12_device = nullptr;
};

} // namespace dlstreamer
