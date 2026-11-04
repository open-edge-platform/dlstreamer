/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/d3d12/gstd3d12.h>

namespace dlstreamer {

// Textures to draw frames into instead of the frames' own, recycled through a
// GStreamer pool allocator.
//
// A frame's texture may still be one of the decoder's reference pictures or be
// shared with another branch, so it must not be drawn on in place. OpenCL has
// no D3D12 sharing either: it can only reach a D3D12 texture through the D3D11
// view of its shared handle (gst_d3d12_memory_get_d3d11_texture()), which needs
// a texture on a shared heap that allows render target and simultaneous
// access. So the frame is copied on the GPU into a pooled texture created that
// way, which then replaces the frame's memory in the buffer.
//
// One pool serves one stream and is not thread-safe. Messages go to the debug
// category given at construction.
class D3D12TargetPool {
  public:
    explicit D3D12TargetPool(GstDebugCategory *category) : _category(category) {
    }
    ~D3D12TargetPool() {
        reset();
    }

    D3D12TargetPool(const D3D12TargetPool &) = delete;
    D3D12TargetPool &operator=(const D3D12TargetPool &) = delete;

    // Swaps the buffer's D3D12 texture for a pooled copy of it. Returns false,
    // leaving the buffer as it was, unless the buffer holds a single D3D12
    // texture that could be copied. The copy is only queued: it becomes the
    // fence of the pooled memory, which D3D12 consumers wait for on the GPU,
    // and gst_d3d12_memory_sync() and CPU maps on the CPU.
    bool replace(GstBuffer *buffer, const GstVideoInfo *info) {
        if (gst_buffer_n_memory(buffer) != 1)
            return false;
        GstMemory *frame = gst_buffer_peek_memory(buffer, 0);
        if (!gst_is_d3d12_memory(frame))
            return false;
        GstMemory *target = acquire(GST_D3D12_MEMORY_CAST(frame));
        if (!target)
            return false;

        // The copy keeps the buffer it reads from alive until the GPU is done
        // with it, so it reads the frame through a buffer of its own: the
        // frame's buffer must stay writable for the swap.
        GstBuffer *src = gst_buffer_new();
        gst_buffer_append_memory(src, gst_memory_ref(frame));
        GstBuffer *dst = gst_buffer_new();
        gst_buffer_append_memory(dst, target);
        const bool copied = gst_d3d12_buffer_copy_into(dst, src, info);
        gst_buffer_unref(src);
        if (copied)
            gst_buffer_replace_memory(buffer, 0, gst_memory_ref(target));
        else
            GST_CAT_WARNING(_category, "Cannot copy the frame into a pooled D3D12 texture; the buffer keeps its own");
        gst_buffer_unref(dst);
        return copied;
    }

  private:
    // A pooled texture the size and format of `frame`'s, or nullptr.
    GstMemory *acquire(GstD3D12Memory *frame) {
        ID3D12Resource *texture = gst_d3d12_memory_get_resource_handle(frame);
        if (!texture)
            return nullptr;
        const D3D12_RESOURCE_DESC desc = texture->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
            return nullptr;

        if (_device != frame->device || desc.Width != _desc.Width || desc.Height != _desc.Height ||
            desc.Format != _desc.Format) {
            GST_CAT_INFO(_category,
                         "D3D12 target pool for %llux%u format %u on device %p (was %llux%u format %u on %p)",
                         static_cast<unsigned long long>(desc.Width), desc.Height, desc.Format,
                         static_cast<void *>(frame->device), static_cast<unsigned long long>(_desc.Width), _desc.Height,
                         _desc.Format, static_cast<void *>(_device));
            reset();
            _desc = {};
            _desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            _desc.Width = desc.Width;
            _desc.Height = desc.Height;
            _desc.DepthOrArraySize = 1;
            _desc.MipLevels = 1;
            _desc.Format = desc.Format;
            _desc.SampleDesc.Count = 1;
            _desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            _desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
            D3D12_HEAP_PROPERTIES heap = {};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            // Not zeroed, which also keeps GStreamer from clearing each new
            // texture: it does that on the device's direct queue while holding
            // the pool's lock, while that queue returns finished frames to the
            // pool holding its own lock, so a pool that grows with frames in
            // flight deadlocks. The copy overwrites the whole texture anyway.
            _device = static_cast<GstD3D12Device *>(gst_object_ref(frame->device));
            _pool = gst_d3d12_pool_allocator_new(frame->device, &heap,
                                                 D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED, &_desc,
                                                 D3D12_RESOURCE_STATE_COMMON, nullptr);
            if (!_pool || !gst_d3d12_allocator_set_active(GST_D3D12_ALLOCATOR(_pool), TRUE)) {
                GST_CAT_WARNING(_category, "Cannot pool %llux%u D3D12 textures of format %u; buffers keep their own",
                                static_cast<unsigned long long>(desc.Width), desc.Height, desc.Format);
                drop_pool();
            }
        }
        if (!_pool) // sticky for this size and format: do not retry every frame
            return nullptr;

        GstMemory *memory = nullptr;
        if (gst_d3d12_pool_allocator_acquire_memory(_pool, &memory) != GST_FLOW_OK) {
            GST_CAT_WARNING(_category, "Cannot allocate %llux%u D3D12 textures of format %u; buffers keep their own",
                            static_cast<unsigned long long>(desc.Width), desc.Height, desc.Format);
            drop_pool();
            return nullptr;
        }
        return memory;
    }

    void drop_pool() {
        if (!_pool)
            return;
        gst_d3d12_allocator_set_active(GST_D3D12_ALLOCATOR(_pool), FALSE);
        gst_object_unref(_pool);
        _pool = nullptr;
    }

    void reset() {
        drop_pool();
        gst_clear_object(&_device);
    }

    GstDebugCategory *_category;
    GstD3D12PoolAllocator *_pool = nullptr;
    GstD3D12Device *_device = nullptr;
    D3D12_RESOURCE_DESC _desc = {};
};

} // namespace dlstreamer
