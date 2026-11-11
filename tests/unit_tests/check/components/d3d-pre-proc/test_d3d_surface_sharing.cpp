/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Tests for the D3D11/D3D12 -> OpenCL surface sharing used to hand pre-processed
// textures to the OpenVINO GPU plugin without a CPU copy.

#include "d3d11_surface_sharing.h"
#include "d3d12_surface_sharing.h"

#include "dlstreamer/d3d11/device_lock.h"
#include "dlstreamer/d3d11/opencl_sharing.h"
#include "utils.h"

#include <gtest/gtest.h>

#include <CL/cl_ext.h>

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace InferenceBackend;
using namespace d3d_test;

namespace {

constexpr DXGI_FORMAT BGRA = DXGI_FORMAT_B8G8R8A8_UNORM;

struct ClContextRelease {
    void operator()(cl_context ctx) const {
        clReleaseContext(ctx);
    }
};
using ClContextPtr = std::unique_ptr<std::remove_pointer_t<cl_context>, ClContextRelease>;

std::vector<cl_platform_id> ClPlatforms() {
    cl_uint count = 0;
    if (clGetPlatformIDs(0, nullptr, &count) != CL_SUCCESS || !count)
        return {};
    std::vector<cl_platform_id> platforms(count);
    if (clGetPlatformIDs(count, platforms.data(), nullptr) != CL_SUCCESS)
        return {};
    return platforms;
}

cl_device_id ContextDevice(cl_context ctx) {
    cl_device_id device = nullptr;
    clGetContextInfo(ctx, CL_CONTEXT_DEVICES, sizeof(device), &device, nullptr);
    return device;
}

// Reads `size` bytes of `buffer` through a queue of our own.
std::vector<uint8_t> ReadClBuffer(cl_context ctx, cl_mem buffer, size_t size) {
    cl_int err = CL_SUCCESS;
    cl_command_queue queue = clCreateCommandQueueWithProperties(ctx, ContextDevice(ctx), nullptr, &err);
    if (err != CL_SUCCESS)
        throw std::runtime_error("clCreateCommandQueueWithProperties failed: " + std::to_string(err));
    std::vector<uint8_t> data(size);
    err = clEnqueueReadBuffer(queue, buffer, CL_TRUE, 0, size, data.data(), 0, nullptr, nullptr);
    clReleaseCommandQueue(queue);
    if (err != CL_SUCCESS)
        throw std::runtime_error("clEnqueueReadBuffer failed: " + std::to_string(err));
    return data;
}

size_t ClBufferSize(cl_mem buffer) {
    size_t size = 0;
    clGetMemObjectInfo(buffer, CL_MEM_SIZE, sizeof(size), &size, nullptr);
    return size;
}

// Expects `data` to hold `pixel` packed as width*height*4 BGRA bytes.
void ExpectPacked(const std::vector<uint8_t> &data, uint32_t width, uint32_t height, const PixelFn &pixel) {
    ASSERT_EQ(data.size(), static_cast<size_t>(width) * height * 4);
    size_t mismatches = 0;
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            const Bgra expected = pixel(x, y);
            if (std::memcmp(&data[(static_cast<size_t>(y) * width + x) * 4], &expected, 4) != 0) {
                if (!mismatches)
                    ADD_FAILURE() << "first mismatch at (" << x << "," << y << ")";
                mismatches++;
            }
        }
    }
    EXPECT_EQ(mismatches, 0u);
}

Bgra InvertedUniquePixel(uint32_t x, uint32_t y) {
    const Bgra p = UniquePixel(x, y);
    return {static_cast<uint8_t>(~p.b), static_cast<uint8_t>(~p.g), static_cast<uint8_t>(~p.r), p.a};
}

// -- D3D11 ------------------------------------------------------------------------

// A context with cl_khr_d3d11_sharing on the device's adapter, or nullptr.
ClContextPtr CreateD3D11ClContext(GstD3D11Device *device) {
    ID3D11Device *d3d11 = gst_d3d11_device_get_device_handle(device);
    for (cl_platform_id platform : ClPlatforms()) {
        const dlstreamer::D3D11SharingFunctions sharing(platform);
        if (!sharing.get_device_ids || !sharing.create_from_texture2d || !sharing.acquire || !sharing.release)
            continue;
        cl_device_id cl_device = nullptr;
        cl_uint num_devices = 0;
        if (sharing.get_device_ids(platform, CL_D3D11_DEVICE_KHR, d3d11, CL_PREFERRED_DEVICES_FOR_D3D11_KHR, 1,
                                   &cl_device, &num_devices) != CL_SUCCESS ||
            !num_devices)
            continue;
        cl_context_properties props[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(platform),
                                         CL_CONTEXT_D3D11_DEVICE_KHR, reinterpret_cast<cl_context_properties>(d3d11),
                                         0};
        cl_int err = CL_SUCCESS;
        cl_context ctx;
        {
            dlstreamer::D3D11DeviceLock lock(device);
            ctx = clCreateContext(props, 1, &cl_device, nullptr, nullptr, &err);
        }
        if (ctx && err == CL_SUCCESS)
            return ClContextPtr(ctx);
    }
    return nullptr;
}

class D3D11SurfaceSharingTest : public testing::Test {
  protected:
    D3D11DevicePtr device;
    ClContextPtr cl_ctx;

    void SetUp() override {
        device = CreateD3D11Device();
        if (!device)
            GTEST_SKIP() << "No D3D11 device";
        cl_ctx = CreateD3D11ClContext(device.get());
        if (!cl_ctx)
            GTEST_SKIP() << "No OpenCL device with cl_khr_d3d11_sharing";
    }

    // A texture like the pre-processing output pool's, written by the GPU
    BufferPtr MakeTexture(uint32_t width, uint32_t height, const PixelFn &pixel) {
        auto buffer =
            AllocD3D11Texture(device.get(), width, height, BGRA, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                              D3D11_RESOURCE_MISC_SHARED);
        Refill(buffer.get(), pixel);
        return buffer;
    }

    void Refill(GstBuffer *buffer, const PixelFn &pixel) {
        FillD3D11(buffer, pixel);
        UploadD3D11(buffer);
    }

    cl_mem Share(D3D11SurfaceSharing &sharing, GstBuffer *buffer, uint32_t width, uint32_t height) {
        // As in production, OpenCL acquires the texture under the device lock
        dlstreamer::D3D11DeviceLock lock(device.get());
        return sharing.create_cl_buffer(D3D11Texture(buffer), width, height);
    }
};

} // namespace

TEST(D3D11SurfaceSharingArgsTest, NullContextThrows) {
    EXPECT_THROW(D3D11SurfaceSharing(nullptr), std::invalid_argument);
}

TEST_F(D3D11SurfaceSharingTest, NullTextureThrows) {
    D3D11SurfaceSharing sharing(cl_ctx.get());
    EXPECT_THROW(sharing.create_cl_buffer(nullptr, 64, 64), std::invalid_argument);
}

TEST_F(D3D11SurfaceSharingTest, CopiesTexturePixels) {
    constexpr uint32_t width = 100, height = 60;
    D3D11SurfaceSharing sharing(cl_ctx.get());
    auto texture = MakeTexture(width, height, UniquePixel);

    cl_mem buffer = Share(sharing, texture.get(), width, height);

    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(ClBufferSize(buffer), static_cast<size_t>(width) * height * 4);
    ExpectPacked(ReadClBuffer(cl_ctx.get(), buffer, ClBufferSize(buffer)), width, height, UniquePixel);
}

TEST_F(D3D11SurfaceSharingTest, CachesBufferPerTexture) {
    constexpr uint32_t width = 64, height = 48;
    D3D11SurfaceSharing sharing(cl_ctx.get());
    auto first = MakeTexture(width, height, UniquePixel);
    auto second = MakeTexture(width, height, InvertedUniquePixel);

    cl_mem first_buffer = Share(sharing, first.get(), width, height);
    cl_mem second_buffer = Share(sharing, second.get(), width, height);
    EXPECT_NE(first_buffer, second_buffer);
    ExpectPacked(ReadClBuffer(cl_ctx.get(), second_buffer, ClBufferSize(second_buffer)), width, height,
                 InvertedUniquePixel);

    // A pooled texture comes back with new content: same buffer, current pixels
    Refill(first.get(), InvertedUniquePixel);
    EXPECT_EQ(Share(sharing, first.get(), width, height), first_buffer);
    ExpectPacked(ReadClBuffer(cl_ctx.get(), first_buffer, ClBufferSize(first_buffer)), width, height,
                 InvertedUniquePixel);
}

// -- D3D12 ------------------------------------------------------------------------

namespace {

bool CanImportOpaqueWin32(cl_device_id device) {
    cl_external_memory_handle_type_khr types[16];
    size_t size = 0;
    if (clGetDeviceInfo(device, CL_DEVICE_EXTERNAL_MEMORY_IMPORT_HANDLE_TYPES_KHR, sizeof(types), types, &size) !=
        CL_SUCCESS)
        return false;
    for (size_t i = 0; i < size / sizeof(types[0]); i++)
        if (types[i] == CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR)
            return true;
    return false;
}

// false only when the device reports a LUID and it is not the adapter's.
bool MatchesAdapter(cl_device_id device, const LUID &adapter_luid) {
    cl_bool valid = CL_FALSE;
    if (clGetDeviceInfo(device, CL_DEVICE_LUID_VALID_KHR, sizeof(valid), &valid, nullptr) != CL_SUCCESS || !valid)
        return true;
    cl_uchar luid[CL_LUID_SIZE_KHR] = {};
    if (clGetDeviceInfo(device, CL_DEVICE_LUID_KHR, sizeof(luid), luid, nullptr) != CL_SUCCESS)
        return true;
    return std::memcmp(luid, &adapter_luid, sizeof(adapter_luid)) == 0;
}

// A GPU context that can import the adapter's D3D12 shared buffers, or nullptr.
ClContextPtr CreateD3D12ClContext(GstD3D12Device *device) {
    const LUID adapter_luid = gst_d3d12_device_get_device_handle(device)->GetAdapterLuid();
    for (cl_platform_id platform : ClPlatforms()) {
        cl_uint count = 0;
        if (clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &count) != CL_SUCCESS || !count)
            continue;
        std::vector<cl_device_id> devices(count);
        clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, count, devices.data(), nullptr);
        for (cl_device_id cl_device : devices) {
            if (!CanImportOpaqueWin32(cl_device) || !MatchesAdapter(cl_device, adapter_luid))
                continue;
            cl_int err = CL_SUCCESS;
            cl_context ctx = clCreateContext(nullptr, 1, &cl_device, nullptr, nullptr, &err);
            if (ctx && err == CL_SUCCESS)
                return ClContextPtr(ctx);
        }
    }
    return nullptr;
}

class D3D12SurfaceSharingTest : public testing::Test {
  protected:
    D3D12DevicePtr device;
    ClContextPtr cl_ctx;

    void SetUp() override {
        device = CreateD3D12Device();
        if (!device)
            GTEST_SKIP() << "No D3D12 device";
        cl_ctx = CreateD3D12ClContext(device.get());
        if (!cl_ctx)
            GTEST_SKIP() << "No OpenCL GPU device importing OPAQUE_WIN32 external memory";
    }

    // A texture like the pre-processing output pool's
    BufferPtr MakeTexture(uint32_t width, uint32_t height, const PixelFn &pixel) {
        auto buffer = AllocD3D12Texture(device.get(), width, height, BGRA, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        FillD3D12(buffer.get(), pixel);
        return buffer;
    }

    // Writes the CPU-filled pixels to the texture on the GPU, as the converter would, and
    // shares the texture ordered after that write through its fence.
    cl_mem Share(D3D12SurfaceSharing &sharing, GstBuffer *buffer, uint32_t width, uint32_t height) {
        auto *mem = GST_D3D12_MEMORY_CAST(gst_buffer_peek_memory(buffer, 0));
        GstMapInfo map;
        if (!gst_memory_map(GST_MEMORY_CAST(mem), &map, static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_D3D12)))
            throw std::runtime_error("failed to map D3D12 texture");
        gst_memory_unmap(GST_MEMORY_CAST(mem), &map);

        ID3D12Fence *fence = nullptr;
        guint64 fence_value = 0;
        gst_d3d12_memory_get_fence(mem, &fence, &fence_value);
        cl_mem result =
            sharing.create_cl_buffer(device.get(), D3D12Resource(buffer), 0, width, height, fence, fence_value);
        if (fence)
            fence->Release();
        return result;
    }
};

} // namespace

TEST(D3D12SurfaceSharingArgsTest, NullContextThrows) {
    EXPECT_THROW(D3D12SurfaceSharing(nullptr), std::invalid_argument);
}

TEST_F(D3D12SurfaceSharingTest, NullDeviceOrTextureThrows) {
    D3D12SurfaceSharing sharing(cl_ctx.get());
    auto texture = MakeTexture(64, 64, UniquePixel);
    EXPECT_THROW(sharing.create_cl_buffer(nullptr, D3D12Resource(texture.get()), 0, 64, 64, nullptr, 0),
                 std::invalid_argument);
    EXPECT_THROW(sharing.create_cl_buffer(device.get(), nullptr, 0, 64, 64, nullptr, 0), std::invalid_argument);
}

TEST_F(D3D12SurfaceSharingTest, CopiesTexturePixels) {
    // 64 pixels wide: rows are already 256-byte aligned, so the plain buffer copy is used.
    // 100 pixels wide: 400-byte rows are padded to 512 in the shared buffer, so the rows are repacked.
    for (uint32_t width : {64u, 100u}) {
        SCOPED_TRACE(testing::Message() << "width " << width);
        constexpr uint32_t height = 60;
        D3D12SurfaceSharing sharing(cl_ctx.get());
        auto texture = MakeTexture(width, height, UniquePixel);

        cl_mem buffer = Share(sharing, texture.get(), width, height);

        ASSERT_NE(buffer, nullptr);
        EXPECT_EQ(ClBufferSize(buffer), static_cast<size_t>(width) * height * 4);
        ExpectPacked(ReadClBuffer(cl_ctx.get(), buffer, ClBufferSize(buffer)), width, height, UniquePixel);
    }
}

TEST_F(D3D12SurfaceSharingTest, CachesBufferPerTexture) {
    constexpr uint32_t width = 100, height = 48;
    D3D12SurfaceSharing sharing(cl_ctx.get());
    auto first = MakeTexture(width, height, UniquePixel);
    auto second = MakeTexture(width, height, InvertedUniquePixel);

    cl_mem first_buffer = Share(sharing, first.get(), width, height);
    cl_mem second_buffer = Share(sharing, second.get(), width, height);
    EXPECT_NE(first_buffer, second_buffer);
    ExpectPacked(ReadClBuffer(cl_ctx.get(), second_buffer, ClBufferSize(second_buffer)), width, height,
                 InvertedUniquePixel);

    // A pooled texture comes back with new content: same buffer, current pixels
    FillD3D12(first.get(), InvertedUniquePixel);
    EXPECT_EQ(Share(sharing, first.get(), width, height), first_buffer);
    ExpectPacked(ReadClBuffer(cl_ctx.get(), first_buffer, ClBufferSize(first_buffer)), width, height,
                 InvertedUniquePixel);
}
