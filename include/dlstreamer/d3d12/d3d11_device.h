/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/d3d11/gstd3d11.h>
#include <gst/d3d12/gstd3d12.h>

#include <map>
#include <mutex>

namespace dlstreamer {

// The D3D11 device on the adapter of `device` through which OpenCL reaches D3D12 textures, or nullptr if it can't be
// created. OpenCL shares D3D11 textures but not D3D12 ones, so it reads and writes a D3D12 texture through the D3D11
// view of its shared handle (gst_d3d12_memory_get_d3d11_texture(), see D3D12TargetPool), opened on a D3D11 device of
// the texture's adapter. One device per adapter, created on first use and never released, as GStreamer keeps its D3D12
// devices: the views of a texture are then opened once, and the users of an adapter share OpenCL contexts on it.
inline GstD3D11Device *d3d11_device_for(GstD3D12Device *device) {
    gint64 luid = 0;
    g_object_get(device, "adapter-luid", &luid, nullptr);

    static std::mutex mutex;
    static std::map<gint64, GstD3D11Device *> devices;
    std::lock_guard<std::mutex> lock(mutex);
    auto [it, created] = devices.try_emplace(luid, nullptr);
    if (created) {
        it->second = gst_d3d11_device_new_for_adapter_luid(luid, D3D11_CREATE_DEVICE_BGRA_SUPPORT);
        if (it->second) // never released: not a leak for leak tracers
            GST_OBJECT_FLAG_SET(it->second, GST_OBJECT_FLAG_MAY_BE_LEAKED);
        else
            GST_WARNING("Cannot create a D3D11 device for D3D12 adapter %" G_GINT64_FORMAT, luid);
    }
    return it->second;
}

} // namespace dlstreamer
