/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/tensor.h"
#include <d3d12.h>

namespace dlstreamer {

namespace tensor::key {
static constexpr auto d3d12_resource_ptr = "d3d12_resource_ptr";     // ID3D12Resource*
static constexpr auto d3d12_subresource_index = "d3d12_subresource"; // Subresource index of plane 0
}; // namespace tensor::key

class D3D12Tensor : public BaseTensor {
  public:
    D3D12Tensor(ID3D12Resource *resource, int plane_index, const TensorInfo &info, ContextPtr context)
        : BaseTensor(MemoryType::D3D12, info, tensor::key::d3d12_resource_ptr, context), _resource(resource) {
        set_handle(tensor::key::d3d12_resource_ptr, reinterpret_cast<handle_t>(_resource));
        set_handle(tensor::key::plane_index, plane_index);
    }

    ID3D12Resource *d3d12_resource() {
        return _resource;
    }

    int plane_index() {
        return handle(tensor::key::plane_index);
    }

    int subresource_index() {
        return handle(tensor::key::d3d12_subresource_index, 0);
    }

    int offset_x() {
        return handle(tensor::key::offset_x, 0);
    }

    int offset_y() {
        return handle(tensor::key::offset_y, 0);
    }

  protected:
    ID3D12Resource *_resource;
};

using D3D12TensorPtr = std::shared_ptr<D3D12Tensor>;

} // namespace dlstreamer
