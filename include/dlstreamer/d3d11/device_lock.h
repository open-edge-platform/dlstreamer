/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/d3d11/gstd3d11device.h>

namespace dlstreamer {

// RAII wrapper for gst_d3d11_device_lock/unlock. GStreamer's own guard lives in
// a private header (gstd3d11-private.h); the D3D11 immediate context is shared
// with every element on the device and is not thread-safe, so GPU work must be
// bracketed by this lock. The underlying mutex is recursive.
class D3D11DeviceLock {
  public:
    explicit D3D11DeviceLock(GstD3D11Device *device) : _device(device) {
        gst_d3d11_device_lock(_device);
    }
    ~D3D11DeviceLock() {
        gst_d3d11_device_unlock(_device);
    }

    D3D11DeviceLock(const D3D11DeviceLock &) = delete;
    D3D11DeviceLock &operator=(const D3D11DeviceLock &) = delete;

  private:
    GstD3D11Device *_device;
};

} // namespace dlstreamer
