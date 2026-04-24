/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>

class GstBufferMapGuard {
  public:
    explicit GstBufferMapGuard(GstBuffer *buffer, GstMapInfo *map_info) : _buffer(buffer), _map_info(map_info) {
    }

    ~GstBufferMapGuard() {
        if (_buffer && _map_info)
            gst_buffer_unmap(_buffer, _map_info);
    }

    GstBufferMapGuard(const GstBufferMapGuard &) = delete;
    GstBufferMapGuard &operator=(const GstBufferMapGuard &) = delete;
    GstBufferMapGuard(GstBufferMapGuard &&) = delete;
    GstBufferMapGuard &operator=(GstBufferMapGuard &&) = delete;

  private:
    GstBuffer *_buffer;
    GstMapInfo *_map_info;
};
