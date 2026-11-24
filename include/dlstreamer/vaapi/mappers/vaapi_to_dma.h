/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/frame.h"
#include "dlstreamer/base/memory_mapper.h"
#include "dlstreamer/dma/tensor.h"
#include "dlstreamer/vaapi/frame.h"

#include <va/va_backend.h>
#include <va/va_drmcommon.h>

namespace dlstreamer {

class MemoryMapperVAAPIToDMA final : public BaseMemoryMapper {
  public:
    MemoryMapperVAAPIToDMA(const ContextPtr &input_context, const ContextPtr &output_context)
        : BaseMemoryMapper(input_context, output_context) {
        DLS_CHECK(input_context)
        auto va_display = input_context->handle(BaseContext::key::va_display);
        DLS_CHECK(va_display)
        DLS_CHECK(_driver_context = reinterpret_cast<VADisplayContextP>(va_display)->pDriverContext);
    }

    TensorPtr map(TensorPtr src, AccessMode mode) override {
        auto src_frame = std::make_shared<BaseFrame>(MediaType::Tensors, 0, TensorVector({src}));
        return map(src_frame, mode)->tensor();
    }

    FramePtr map(FramePtr src, AccessMode /*mode*/) override {
        auto tensor = src->tensor(0);
        auto va_surface = ptr_cast<VAAPITensor>(tensor)->va_surface();

        VADRMPRIMESurfaceDescriptor prime_desc{};
        DLS_CHECK(_driver_context->vtable->vaExportSurfaceHandle(
                      _driver_context, va_surface, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2, VA_EXPORT_SURFACE_READ_WRITE,
                      &prime_desc) == VA_STATUS_SUCCESS);
        // printf("vaExportSurfaceHandle: %d -> %d\n", va_surface, prime_desc.objects[0].fd);

        // Update stride and offset for each plane
        TensorVector tensors;
        int plane_num = 0;
        int last_dma_fd = -1;
        for (uint32_t i = 0; i < prime_desc.num_layers; i++) {
            const auto layer = &prime_desc.layers[i];
            for (uint32_t j = 0; j < layer->num_planes; j++) {
                if (plane_num >= (int)src->num_tensors())
                    break;
                TensorInfo info = src->tensor(plane_num)->info();
                int object_index = layer->object_index[j];
                int dma_fd = prime_desc.objects[object_index].fd;
                uint64_t drm_modifier = prime_desc.objects[object_index].drm_format_modifier;
                info.stride[ImageLayout(info.shape).w_position() - 1] = layer->pitch[j];
                // only one tensor object takes ownership of each dma-fd handle
                auto dst =
                    std::make_shared<DMATensor>(dma_fd, drm_modifier, info, dma_fd != last_dma_fd, _output_context);
                last_dma_fd = dma_fd;
                dst->set_handle(tensor::key::offset, layer->offset[j]);
                tensors.push_back(dst);
                plane_num++;
            }
        }

        auto ret = std::make_shared<BaseFrame>(src->media_type(), src->format(), tensors);
        ret->set_parent(src);
        return ret;
    }

  private:
    VADriverContextP _driver_context;
};

} // namespace dlstreamer
