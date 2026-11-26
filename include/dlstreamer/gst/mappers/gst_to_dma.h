/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/dma/tensor.h"
#include "dlstreamer/gst/frame.h"
#include <gst/allocators/allocators.h>
#include <gst/video/video.h>

namespace dlstreamer {

class MemoryMapperGSTToDMA : public BaseMemoryMapper {
  public:
    // drm_modifier: DRM format modifier of the buffers, from the 'drm-format' field of DMA_DRM caps (0 is linear).
    // GstMemory does not carry it.
    MemoryMapperGSTToDMA(const ContextPtr &input_context, const ContextPtr &output_context, uint64_t drm_modifier = 0)
        : BaseMemoryMapper(input_context, output_context), _drm_modifier(drm_modifier) {
    }

    TensorPtr map(TensorPtr src, AccessMode /*mode*/) override {
        return map_plane(src, nullptr);
    }

    FramePtr map(FramePtr src, AccessMode mode) override {
        // The GstVideoMeta of a video buffer gives the real stride and offset of each plane. The video info of the
        // caps only gives the default layout: VA surfaces, for example, have their chroma plane after a padded height.
        GstVideoMeta *meta = nullptr;
        auto gst_frame = std::dynamic_pointer_cast<GSTFrame>(src);
        if (gst_frame && gst_frame->video_info() && gst_frame->gst_buffer())
            meta = gst_buffer_get_video_meta(gst_frame->gst_buffer());

        TensorVector tensors;
        for (auto &tensor : src)
            tensors.push_back(map_plane(tensor, meta));
        if (tensors.empty())
            return BaseMemoryMapper::map(src, mode);
        auto dst = std::make_shared<BaseFrame>(src->media_type(), src->format(), tensors);
        dst->set_parent(src);
        return dst;
    }

  private:
    uint64_t _drm_modifier;

    TensorPtr map_plane(const TensorPtr &src, const GstVideoMeta *meta) {
        auto gst_tensor = ptr_cast<GSTTensor>(src);
        int dma_fd = gst_dmabuf_memory_get_fd(gst_tensor->gst_memory());
        if (dma_fd < 0)
            throw std::runtime_error("Failed to import DMA buffer FD");

        TensorInfo info = src->info();
        size_t data_offset = src->handle(tensor::key::offset, 0);
        const int plane = gst_tensor->plane_index();
        if (meta && plane >= 0 && static_cast<guint>(plane) < meta->n_planes) {
            const int row_stride_index = ImageLayout(info.shape).w_position() - 1;
            if (row_stride_index >= 0 && static_cast<size_t>(row_stride_index) < info.stride.size())
                info.stride[row_stride_index] = meta->stride[plane];
            data_offset = meta->offset[plane];
        }

        auto dst = std::make_shared<DMATensor>(dma_fd, _drm_modifier, info);

        // offset_x/y
        int offset_x = gst_tensor->offset_x();
        int offset_y = gst_tensor->offset_y();
        if (offset_x || offset_y) {
            ImageInfo image_info(info);
            data_offset += offset_y * image_info.width_stride() + offset_x * image_info.channels_stride();
        }

        dst->set_handle(tensor::key::offset, data_offset);
        dst->set_parent(src);
        return dst;
    }
};

} // namespace dlstreamer
