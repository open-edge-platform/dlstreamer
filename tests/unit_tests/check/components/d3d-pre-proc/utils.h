/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gtest/gtest.h>

#include <gst/d3d11/gstd3d11.h>
#include <gst/d3d12/gstd3d12.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace d3d_test {

struct Bgra {
    uint8_t b, g, r, a;
};

namespace colors {
constexpr Bgra black = {0, 0, 0, 255};
constexpr Bgra white = {255, 255, 255, 255};
constexpr Bgra red = {0, 0, 255, 255};
constexpr Bgra green = {0, 255, 0, 255};
constexpr Bgra blue = {255, 0, 0, 255};
constexpr Bgra yellow = {0, 255, 255, 255};
} // namespace colors

using PixelFn = std::function<Bgra(uint32_t x, uint32_t y)>;

struct BufferUnref {
    void operator()(GstBuffer *buf) const {
        gst_buffer_unref(buf);
    }
};
using BufferPtr = std::unique_ptr<GstBuffer, BufferUnref>;

template <typename T>
struct ObjectUnref {
    void operator()(T *obj) const {
        gst_object_unref(obj);
    }
};
using D3D11DevicePtr = std::unique_ptr<GstD3D11Device, ObjectUnref<GstD3D11Device>>;
using D3D12DevicePtr = std::unique_ptr<GstD3D12Device, ObjectUnref<GstD3D12Device>>;

// Packed BGRA pixels read back from a texture.
struct BgraImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<Bgra> pixels;

    const Bgra &at(uint32_t x, uint32_t y) const {
        return pixels.at(static_cast<size_t>(y) * width + x);
    }
};

testing::AssertionResult ColorNear(const Bgra &actual, const Bgra &expected, int tolerance);

// Checks every pixel of [x0, x1) x [y0, y1).
testing::AssertionResult RegionIs(const BgraImage &image, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                                  const Bgra &expected, int tolerance);

// A frame-sized pattern: the rectangle (x, y, width, height) is split into red / green /
// blue / yellow quadrants (top-left, top-right, bottom-left, bottom-right), the rest is white.
PixelFn QuadrantPattern(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

// Expects the output to show the four quadrants of QuadrantPattern filling
// (x0, y0) - (x1, y1), with `inset` pixels at each edge excluded from the check:
// bilinear sampling may blend the pattern edge with its neighbours there.
void ExpectQuadrants(const BgraImage &image, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t inset);

// A pattern with a distinct value in every channel of every pixel of a small image.
Bgra UniquePixel(uint32_t x, uint32_t y);

// -- D3D11 --------------------------------------------------------------------

// nullptr when there is no D3D11 adapter.
D3D11DevicePtr CreateD3D11Device();

BufferPtr AllocD3D11Texture(GstD3D11Device *device, uint32_t width, uint32_t height, DXGI_FORMAT format,
                            UINT bind_flags, UINT misc_flags = 0);

// CPU-writes a BGRA texture.
void FillD3D11(GstBuffer *buffer, const PixelFn &pixel);
// CPU-writes a uniform NV12 texture.
void FillD3D11Nv12(GstBuffer *buffer, uint8_t y, uint8_t u, uint8_t v);
// Pushes CPU writes to the GPU texture, as a GPU consumer that maps the memory would.
void UploadD3D11(GstBuffer *buffer);

BgraImage ReadD3D11(GstBuffer *buffer);

ID3D11Texture2D *D3D11Texture(GstBuffer *buffer);

// -- D3D12 --------------------------------------------------------------------

// nullptr when there is no D3D12 adapter.
D3D12DevicePtr CreateD3D12Device();

// SIMULTANEOUS_ACCESS is always added: GstD3D12Memory needs it for CPU access.
BufferPtr AllocD3D12Texture(GstD3D12Device *device, uint32_t width, uint32_t height, DXGI_FORMAT format,
                            D3D12_RESOURCE_FLAGS flags);

void FillD3D12(GstBuffer *buffer, const PixelFn &pixel);
void FillD3D12Nv12(GstBuffer *buffer, uint8_t y, uint8_t u, uint8_t v);

BgraImage ReadD3D12(GstBuffer *buffer);

ID3D12Resource *D3D12Resource(GstBuffer *buffer);

void WaitFence(ID3D12Fence *fence, uint64_t value);

} // namespace d3d_test
