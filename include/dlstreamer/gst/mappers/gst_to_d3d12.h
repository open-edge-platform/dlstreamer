/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/memory_mapper.h"
#include "dlstreamer/d3d12/context.h"
#include "dlstreamer/d3d12/tensor.h"
#include "dlstreamer/gst/frame.h"

#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/d3d12/gstd3d12.h>

namespace dlstreamer {

class MemoryMapperGSTToD3D12 : public BaseMemoryMapper {
  public:
    using BaseMemoryMapper::BaseMemoryMapper;

    TensorPtr map(TensorPtr src, AccessMode /*mode*/) override {
        auto src_gst = ptr_cast<GSTTensor>(src);

        GstMemory *mem = src_gst->gst_memory();
        if (!gst_is_d3d12_memory(mem))
            throw std::runtime_error("MemoryMapperGSTToD3D12: GstMemory is not D3D12 memory");

        auto *dmem = GST_D3D12_MEMORY_CAST(mem);
        ID3D12Resource *resource = gst_d3d12_memory_get_resource_handle(dmem);
        guint subresource_idx = 0;
        gst_d3d12_memory_get_subresource_index(dmem, 0, &subresource_idx);

        auto ret = std::make_shared<D3D12Tensor>(resource, src_gst->plane_index(), src->info(), _output_context);

        ret->set_handle(tensor::key::d3d12_subresource_index, subresource_idx);
        ret->set_handle(tensor::key::gst_memory, reinterpret_cast<Tensor::handle_t>(mem));
        ret->set_handle(tensor::key::offset_x, src_gst->offset_x());
        ret->set_handle(tensor::key::offset_y, src_gst->offset_y());
        ret->set_parent(src);
        return ret;
    }
};

} // namespace dlstreamer
