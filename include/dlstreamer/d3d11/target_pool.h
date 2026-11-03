/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/d3d11/device_lock.h"

#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/d3d11/gstd3d11.h>

namespace dlstreamer {

// Textures to draw frames into instead of the frames' own, recycled through a
// GStreamer pool allocator.
//
// A frame's texture may still be one of the decoder's reference pictures or be
// shared with another branch, so it must not be drawn on in place, and OpenCL
// can only write textures created shared (D3D11_RESOURCE_MISC_SHARED), which a
// decoder's are not. The copy-on-write of a GST_MAP_WRITE solves the first, but
// creates and destroys a texture every frame, and a texture released on a
// streaming thread while another uses the immediate context crashes the Intel
// driver. So the frame is copied on the GPU into a pooled shared texture, which
// then replaces the frame's memory in the buffer.
//
// One pool serves one stream and is not thread-safe. Messages go to the debug
// category given at construction.
class D3D11TargetPool {
  public:
    explicit D3D11TargetPool(GstDebugCategory *category) : _category(category) {
    }
    ~D3D11TargetPool() {
        reset();
    }

    D3D11TargetPool(const D3D11TargetPool &) = delete;
    D3D11TargetPool &operator=(const D3D11TargetPool &) = delete;

    // Swaps the buffer's D3D11 texture for a pooled copy of it. Returns false,
    // leaving the buffer as it was, unless the buffer holds a single D3D11
    // texture that could be copied.
    bool replace(GstBuffer *buffer) {
        if (gst_buffer_n_memory(buffer) != 1)
            return false;
        GstMemory *frame = gst_buffer_peek_memory(buffer, 0);
        if (!gst_is_d3d11_memory(frame) ||
            gst_d3d11_memory_get_native_type(GST_D3D11_MEMORY_CAST(frame)) != GST_D3D11_MEMORY_NATIVE_TYPE_TEXTURE_2D)
            return false;
        GstD3D11Device *device = GST_D3D11_MEMORY_CAST(frame)->device;
        GstMemory *target = acquire(GST_D3D11_MEMORY_CAST(frame));
        if (!target)
            return false;

        // The maps upload whatever a CPU writer left in staging memory, and make
        // a later CPU map download the copy.
        GstMapInfo frame_info, target_info;
        if (!gst_memory_map(frame, &frame_info, static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_D3D11))) {
            gst_memory_unref(target);
            return false;
        }
        if (!gst_memory_map(target, &target_info, static_cast<GstMapFlags>(GST_MAP_WRITE | GST_MAP_D3D11))) {
            gst_memory_unmap(frame, &frame_info);
            gst_memory_unref(target);
            return false;
        }
        {
            D3D11DeviceLock lock(device);
            gst_d3d11_device_get_device_context_handle(device)->CopySubresourceRegion(
                reinterpret_cast<ID3D11Resource *>(target_info.data), 0, 0, 0, 0,
                reinterpret_cast<ID3D11Resource *>(frame_info.data), GPOINTER_TO_UINT(frame_info.user_data[0]),
                nullptr);
        }
        gst_memory_unmap(target, &target_info);
        gst_memory_unmap(frame, &frame_info);

        gst_buffer_replace_memory(buffer, 0, target);
        return true;
    }

  private:
    // A pooled texture the size and format of `frame`'s, or nullptr.
    GstMemory *acquire(GstD3D11Memory *frame) {
        auto *texture = reinterpret_cast<ID3D11Texture2D *>(gst_d3d11_memory_get_resource_handle(frame));
        if (!texture)
            return nullptr;
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);

        if (_device != frame->device || desc.Width != _desc.Width || desc.Height != _desc.Height ||
            desc.Format != _desc.Format) {
            GST_CAT_INFO(_category, "D3D11 target pool for %ux%u format %u on device %p (was %ux%u format %u on %p)",
                         desc.Width, desc.Height, desc.Format, static_cast<void *>(frame->device), _desc.Width,
                         _desc.Height, _desc.Format, static_cast<void *>(_device));
            reset();
            _desc = {};
            _desc.Width = desc.Width;
            _desc.Height = desc.Height;
            _desc.MipLevels = 1;
            _desc.ArraySize = 1;
            _desc.Format = desc.Format;
            _desc.SampleDesc.Count = 1;
            _desc.Usage = D3D11_USAGE_DEFAULT;
            _desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            _desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
            _device = static_cast<GstD3D11Device *>(gst_object_ref(frame->device));
            _pool = gst_d3d11_pool_allocator_new(frame->device, &_desc);
            if (!_pool || !gst_d3d11_allocator_set_active(GST_D3D11_ALLOCATOR(_pool), TRUE)) {
                GST_CAT_WARNING(_category, "Cannot pool %ux%u D3D11 textures of format %u; buffers keep their own",
                                desc.Width, desc.Height, desc.Format);
                drop_pool();
            }
        }
        if (!_pool) // sticky for this size and format: do not retry every frame
            return nullptr;

        GstMemory *memory = nullptr;
        if (gst_d3d11_pool_allocator_acquire_memory(_pool, &memory) != GST_FLOW_OK) {
            GST_CAT_WARNING(_category, "Cannot allocate %ux%u D3D11 textures of format %u; buffers keep their own",
                            desc.Width, desc.Height, desc.Format);
            drop_pool();
            return nullptr;
        }
        return memory;
    }

    void drop_pool() {
        if (!_pool)
            return;
        {
            // Frees the idle textures, which must not race with the immediate
            // context either.
            D3D11DeviceLock lock(_device);
            gst_d3d11_allocator_set_active(GST_D3D11_ALLOCATOR(_pool), FALSE);
        }
        gst_object_unref(_pool);
        _pool = nullptr;
    }

    void reset() {
        drop_pool();
        gst_clear_object(&_device);
    }

    GstDebugCategory *_category;
    GstD3D11PoolAllocator *_pool = nullptr;
    GstD3D11Device *_device = nullptr;
    D3D11_TEXTURE2D_DESC _desc = {};
};

} // namespace dlstreamer
