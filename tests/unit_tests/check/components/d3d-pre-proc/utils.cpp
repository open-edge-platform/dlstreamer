/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "utils.h"

#include "dlstreamer/d3d11/device_lock.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace d3d_test {

namespace {

std::string ToString(const Bgra &c) {
    return "(b=" + std::to_string(c.b) + " g=" + std::to_string(c.g) + " r=" + std::to_string(c.r) +
           " a=" + std::to_string(c.a) + ")";
}

bool Near(const Bgra &actual, const Bgra &expected, int tolerance) {
    return std::abs(actual.b - expected.b) <= tolerance && std::abs(actual.g - expected.g) <= tolerance &&
           std::abs(actual.r - expected.r) <= tolerance && std::abs(actual.a - expected.a) <= tolerance;
}

struct MemoryMap {
    GstMemory *mem;
    GstMapInfo info;

    MemoryMap(GstMemory *mem, GstMapFlags flags) : mem(mem) {
        if (!gst_memory_map(mem, &info, flags))
            throw std::runtime_error("failed to map memory");
    }
    ~MemoryMap() {
        gst_memory_unmap(mem, &info);
    }
    uint8_t *data() const {
        return info.data;
    }
};

// D3D11 CPU maps go through a staging texture; the stride is known once it is mapped.
uint32_t D3D11Stride(GstMemory *mem) {
    guint stride = 0;
    if (!gst_d3d11_memory_get_resource_stride(GST_D3D11_MEMORY_CAST(mem), &stride))
        throw std::runtime_error("failed to get D3D11 resource stride");
    return stride;
}

D3D11_TEXTURE2D_DESC D3D11Desc(GstMemory *mem) {
    D3D11_TEXTURE2D_DESC desc;
    if (!gst_d3d11_memory_get_texture_desc(GST_D3D11_MEMORY_CAST(mem), &desc))
        throw std::runtime_error("failed to get D3D11 texture desc");
    return desc;
}

// The CPU staging layout GstD3D12Memory uses: one footprint per plane.
struct D3D12Layout {
    D3D12_RESOURCE_DESC desc;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT planes[2] = {};
};

D3D12Layout D3D12StagingLayout(GstMemory *mem) {
    auto *dmem = GST_D3D12_MEMORY_CAST(mem);
    D3D12Layout layout;
    layout.desc = gst_d3d12_memory_get_resource_handle(dmem)->GetDesc();
    const guint num_planes = gst_d3d12_memory_get_plane_count(dmem);
    if (num_planes > 2)
        throw std::runtime_error("unexpected D3D12 plane count");
    D3D12_RESOURCE_DESC desc = layout.desc;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    gst_d3d12_device_get_device_handle(dmem->device)
        ->GetCopyableFootprints(&desc, 0, num_planes, 0, layout.planes, nullptr, nullptr, nullptr);
    return layout;
}

void FillNv12(uint8_t *y_plane, uint32_t y_stride, uint8_t *uv_plane, uint32_t uv_stride, uint32_t width,
              uint32_t height, uint8_t y, uint8_t u, uint8_t v) {
    for (uint32_t row = 0; row < height; row++)
        std::memset(y_plane + static_cast<size_t>(row) * y_stride, y, width);
    for (uint32_t row = 0; row < height / 2; row++) {
        uint8_t *line = uv_plane + static_cast<size_t>(row) * uv_stride;
        for (uint32_t col = 0; col < width / 2; col++) {
            line[2 * col] = u;
            line[2 * col + 1] = v;
        }
    }
}

void FillBgra(uint8_t *data, uint32_t stride, uint32_t width, uint32_t height, const PixelFn &pixel) {
    for (uint32_t y = 0; y < height; y++) {
        auto *line = reinterpret_cast<Bgra *>(data + static_cast<size_t>(y) * stride);
        for (uint32_t x = 0; x < width; x++)
            line[x] = pixel(x, y);
    }
}

BgraImage ReadBgra(const uint8_t *data, uint32_t stride, uint32_t width, uint32_t height) {
    BgraImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<size_t>(width) * height);
    for (uint32_t y = 0; y < height; y++)
        std::memcpy(&image.pixels[static_cast<size_t>(y) * width], data + static_cast<size_t>(y) * stride,
                    static_cast<size_t>(width) * sizeof(Bgra));
    return image;
}

} // namespace

testing::AssertionResult ColorNear(const Bgra &actual, const Bgra &expected, int tolerance) {
    if (Near(actual, expected, tolerance))
        return testing::AssertionSuccess();
    return testing::AssertionFailure() << "got " << ToString(actual) << ", expected " << ToString(expected);
}

testing::AssertionResult RegionIs(const BgraImage &image, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                                  const Bgra &expected, int tolerance) {
    if (x1 > image.width || y1 > image.height || x0 >= x1 || y0 >= y1)
        return testing::AssertionFailure() << "bad region (" << x0 << "," << y0 << ")-(" << x1 << "," << y1 << ") for "
                                           << image.width << "x" << image.height << " image";
    size_t mismatches = 0;
    uint32_t first_x = 0, first_y = 0;
    for (uint32_t y = y0; y < y1; y++) {
        for (uint32_t x = x0; x < x1; x++) {
            if (!Near(image.at(x, y), expected, tolerance)) {
                if (!mismatches) {
                    first_x = x;
                    first_y = y;
                }
                mismatches++;
            }
        }
    }
    if (!mismatches)
        return testing::AssertionSuccess();
    return testing::AssertionFailure() << mismatches << " pixel(s) of region (" << x0 << "," << y0 << ")-(" << x1 << ","
                                       << y1 << ") differ from " << ToString(expected) << ", first at (" << first_x
                                       << "," << first_y << "): " << ToString(image.at(first_x, first_y));
}

PixelFn QuadrantPattern(uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh) {
    return [=](uint32_t x, uint32_t y) {
        if (x < rx || y < ry || x >= rx + rw || y >= ry + rh)
            return colors::white;
        const bool right = (x - rx) >= rw / 2;
        const bool bottom = (y - ry) >= rh / 2;
        if (!bottom)
            return right ? colors::green : colors::red;
        return right ? colors::yellow : colors::blue;
    };
}

void ExpectQuadrants(const BgraImage &image, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t inset) {
    const uint32_t xm = x0 + (x1 - x0) / 2;
    const uint32_t ym = y0 + (y1 - y0) / 2;
    constexpr int tolerance = 2;
    EXPECT_TRUE(RegionIs(image, x0 + inset, y0 + inset, xm - inset, ym - inset, colors::red, tolerance))
        << "top-left quadrant";
    EXPECT_TRUE(RegionIs(image, xm + inset, y0 + inset, x1 - inset, ym - inset, colors::green, tolerance))
        << "top-right quadrant";
    EXPECT_TRUE(RegionIs(image, x0 + inset, ym + inset, xm - inset, y1 - inset, colors::blue, tolerance))
        << "bottom-left quadrant";
    EXPECT_TRUE(RegionIs(image, xm + inset, ym + inset, x1 - inset, y1 - inset, colors::yellow, tolerance))
        << "bottom-right quadrant";
}

Bgra UniquePixel(uint32_t x, uint32_t y) {
    return {static_cast<uint8_t>(x), static_cast<uint8_t>(y), static_cast<uint8_t>(x * 7 + y * 13),
            static_cast<uint8_t>(255 - ((x + y) & 0x7f))};
}

// -- D3D11 --------------------------------------------------------------------

D3D11DevicePtr CreateD3D11Device() {
    return D3D11DevicePtr(gst_d3d11_device_new(0, D3D11_CREATE_DEVICE_BGRA_SUPPORT));
}

BufferPtr AllocD3D11Texture(GstD3D11Device *device, uint32_t width, uint32_t height, DXGI_FORMAT format,
                            UINT bind_flags, UINT misc_flags) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind_flags;
    desc.MiscFlags = misc_flags;
    GstMemory *mem = gst_d3d11_allocator_alloc(nullptr, device, &desc);
    if (!mem)
        throw std::runtime_error("failed to allocate D3D11 texture");
    BufferPtr buffer(gst_buffer_new());
    gst_buffer_append_memory(buffer.get(), mem);
    return buffer;
}

void FillD3D11(GstBuffer *buffer, const PixelFn &pixel) {
    GstMemory *mem = gst_buffer_peek_memory(buffer, 0);
    const auto desc = D3D11Desc(mem);
    MemoryMap map(mem, GST_MAP_WRITE);
    FillBgra(map.data(), D3D11Stride(mem), desc.Width, desc.Height, pixel);
}

void FillD3D11Nv12(GstBuffer *buffer, uint8_t y, uint8_t u, uint8_t v) {
    GstMemory *mem = gst_buffer_peek_memory(buffer, 0);
    const auto desc = D3D11Desc(mem);
    MemoryMap map(mem, GST_MAP_WRITE);
    const uint32_t stride = D3D11Stride(mem);
    // A mapped NV12 staging texture holds the UV plane right after the Y plane
    FillNv12(map.data(), stride, map.data() + static_cast<size_t>(stride) * desc.Height, stride, desc.Width,
             desc.Height, y, u, v);
}

void UploadD3D11(GstBuffer *buffer) {
    GstMemory *mem = gst_buffer_peek_memory(buffer, 0);
    { MemoryMap map(mem, static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_D3D11)); }
    auto *device = GST_D3D11_MEMORY_CAST(mem)->device;
    dlstreamer::D3D11DeviceLock lock(device);
    gst_d3d11_device_get_device_context_handle(device)->Flush();
}

BgraImage ReadD3D11(GstBuffer *buffer) {
    GstMemory *mem = gst_buffer_peek_memory(buffer, 0);
    const auto desc = D3D11Desc(mem);
    MemoryMap map(mem, GST_MAP_READ);
    return ReadBgra(map.data(), D3D11Stride(mem), desc.Width, desc.Height);
}

ID3D11Texture2D *D3D11Texture(GstBuffer *buffer) {
    auto *mem = GST_D3D11_MEMORY_CAST(gst_buffer_peek_memory(buffer, 0));
    return static_cast<ID3D11Texture2D *>(gst_d3d11_memory_get_resource_handle(mem));
}

// -- D3D12 --------------------------------------------------------------------

D3D12DevicePtr CreateD3D12Device() {
    return D3D12DevicePtr(gst_d3d12_device_new(0));
}

BufferPtr AllocD3D12Texture(GstD3D12Device *device, uint32_t width, uint32_t height, DXGI_FORMAT format,
                            D3D12_RESOURCE_FLAGS flags) {
    D3D12_HEAP_PROPERTIES heap_props = {};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    GstMemory *mem = gst_d3d12_allocator_alloc(nullptr, device, &heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr);
    if (!mem)
        throw std::runtime_error("failed to allocate D3D12 texture");
    BufferPtr buffer(gst_buffer_new());
    gst_buffer_append_memory(buffer.get(), mem);
    return buffer;
}

void FillD3D12(GstBuffer *buffer, const PixelFn &pixel) {
    GstMemory *mem = gst_buffer_peek_memory(buffer, 0);
    const auto layout = D3D12StagingLayout(mem);
    MemoryMap map(mem, GST_MAP_WRITE);
    FillBgra(map.data() + layout.planes[0].Offset, layout.planes[0].Footprint.RowPitch,
             static_cast<uint32_t>(layout.desc.Width), layout.desc.Height, pixel);
}

void FillD3D12Nv12(GstBuffer *buffer, uint8_t y, uint8_t u, uint8_t v) {
    GstMemory *mem = gst_buffer_peek_memory(buffer, 0);
    const auto layout = D3D12StagingLayout(mem);
    MemoryMap map(mem, GST_MAP_WRITE);
    FillNv12(map.data() + layout.planes[0].Offset, layout.planes[0].Footprint.RowPitch,
             map.data() + layout.planes[1].Offset, layout.planes[1].Footprint.RowPitch,
             static_cast<uint32_t>(layout.desc.Width), layout.desc.Height, y, u, v);
}

BgraImage ReadD3D12(GstBuffer *buffer) {
    GstMemory *mem = gst_buffer_peek_memory(buffer, 0);
    const auto layout = D3D12StagingLayout(mem);
    MemoryMap map(mem, GST_MAP_READ);
    return ReadBgra(map.data() + layout.planes[0].Offset, layout.planes[0].Footprint.RowPitch,
                    static_cast<uint32_t>(layout.desc.Width), layout.desc.Height);
}

ID3D12Resource *D3D12Resource(GstBuffer *buffer) {
    return gst_d3d12_memory_get_resource_handle(GST_D3D12_MEMORY_CAST(gst_buffer_peek_memory(buffer, 0)));
}

void WaitFence(ID3D12Fence *fence, uint64_t value) {
    if (fence->GetCompletedValue() < value && FAILED(fence->SetEventOnCompletion(value, nullptr)))
        throw std::runtime_error("failed to wait for D3D12 fence");
}

} // namespace d3d_test
