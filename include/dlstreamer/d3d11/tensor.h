/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/tensor.h"
#include <d3d11.h>

namespace dlstreamer {

namespace tensor::key {
static constexpr auto d3d11_texture_ptr = "d3d11_texture_ptr";       // ID3D11Texture2D*
static constexpr auto d3d11_subresource_index = "d3d11_subresource"; // Array slice index for texture arrays
}; // namespace tensor::key

class D3D11Tensor : public BaseTensor {
  public:
    D3D11Tensor(ID3D11Texture2D *texture, int plane_index, const TensorInfo &info, ContextPtr context)
        : BaseTensor(MemoryType::D3D11, info, tensor::key::d3d11_texture_ptr, context), _texture(texture) {
        set_handle(tensor::key::d3d11_texture_ptr, reinterpret_cast<handle_t>(_texture));
        set_handle(tensor::key::plane_index, plane_index);
    }

    ID3D11Texture2D *d3d11_texture() {
        return _texture;
    }

    int plane_index() {
        return handle(tensor::key::plane_index);
    }

    int subresource_index() {
        return handle(tensor::key::d3d11_subresource_index, 0);
    }

    int offset_x() {
        return handle(tensor::key::offset_x, 0);
    }

    int offset_y() {
        return handle(tensor::key::offset_y, 0);
    }

  protected:
    ID3D11Texture2D *_texture;
};

using D3D11TensorPtr = std::shared_ptr<D3D11Tensor>;

} // namespace dlstreamer