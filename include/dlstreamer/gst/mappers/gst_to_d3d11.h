/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/memory_mapper.h"
#include "dlstreamer/d3d11/context.h"
#include "dlstreamer/d3d11/tensor.h"
#include "dlstreamer/gst/frame.h"

#define GST_USE_UNSTABLE_API
#include <gst/d3d11/gstd3d11.h>

namespace dlstreamer {

class MemoryMapperGSTToD3D11 : public BaseMemoryMapper {
  public:
    using BaseMemoryMapper::BaseMemoryMapper;

    TensorPtr map(TensorPtr src, AccessMode /*mode*/) override {
        auto src_gst = ptr_cast<GSTTensor>(src);

        // Extract D3D11 texture handle and subresource index from GstMemory
        GstMemory *mem = src_gst->gst_memory();
        void *d3d11_texture_ptr = get_d3d11_texture(mem);
        ID3D11Texture2D *d3d11_texture = static_cast<ID3D11Texture2D *>(d3d11_texture_ptr);
        guint subresource_idx = gst_d3d11_memory_get_subresource_index((GstD3D11Memory *)mem);

        auto ret = std::make_shared<D3D11Tensor>(d3d11_texture, src_gst->plane_index(), src->info(), _output_context);

        ret->set_handle(tensor::key::d3d11_subresource_index, subresource_idx);
        ret->set_handle(tensor::key::offset_x, src_gst->offset_x());
        ret->set_handle(tensor::key::offset_y, src_gst->offset_y());
        ret->set_parent(src);
        return ret;
    }

  protected:
    void *get_d3d11_texture(GstMemory *mem) {
        gboolean is_d3d11 = gst_is_d3d11_memory(mem);
        if (!is_d3d11) {
            throw std::runtime_error("MemoryMapperGSTToD3D11: GstMemory is not D3D11 memory");
        }
        void *d3d11_texture = reinterpret_cast<void *>(gst_d3d11_memory_get_resource_handle((GstD3D11Memory *)mem));
        return d3d11_texture;
    }
};

} // namespace dlstreamer