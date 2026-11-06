/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "d3d12_surface_sharing.h"

#include "inference_backend/logger.h"
#include "inference_backend/opencl_interop_queue.h"

#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <CL/cl_ext.h>
#include <wrl/client.h>

using namespace InferenceBackend;
using Microsoft::WRL::ComPtr;

namespace {

void check_cl(cl_int err, const char *msg) {
    if (err != CL_SUCCESS)
        throw std::runtime_error(std::string(msg) + " (cl_err=" + std::to_string(err) + ")");
}

void check_hr(HRESULT hr, const char *msg) {
    if (FAILED(hr))
        throw std::runtime_error(std::string("D3D12SurfaceSharing: ") + msg + " (HRESULT 0x" +
                                 std::to_string(static_cast<unsigned long>(hr)) + ")");
}

// Copy destination for one pooled texture plus its OpenCL view.
struct TextureCacheEntry {
    ComPtr<ID3D12Resource> shared_buf;
    HANDLE nt_handle = nullptr;
    cl_mem cl_buf = nullptr;  // imported view of shared_buf (row pitch = footprint pitch)
    cl_mem compact = nullptr; // packed copy handed to OpenVINO
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT num_rows = 0;
    uint32_t width = 0, height = 0;

    ~TextureCacheEntry() {
        if (compact)
            clReleaseMemObject(compact);
        if (cl_buf)
            clReleaseMemObject(cl_buf);
        if (nt_handle)
            CloseHandle(nt_handle);
    }
};

// COPY-queue command recorder; one per D3D12 device. Reused across frames because
// every submission is waited for on the CPU before the next one is recorded.
struct CopyRecorder {
    GstD3D12CmdQueue *queue = nullptr; // owned by the GstD3D12Device
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> cl;
    uint64_t last_fence_value = 0;

    void init(GstD3D12Device *gst_device) {
        queue = gst_d3d12_device_get_cmd_queue(gst_device, D3D12_COMMAND_LIST_TYPE_COPY);
        if (!queue)
            throw std::runtime_error("D3D12SurfaceSharing: device has no COPY command queue");
        auto *dev = gst_d3d12_device_get_device_handle(gst_device);
        check_hr(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&alloc)),
                 "CreateCommandAllocator(COPY)");
        check_hr(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, alloc.Get(), nullptr, IID_PPV_ARGS(&cl)),
                 "CreateCommandList(COPY)");
        cl->Close();
    }

    ~CopyRecorder() {
        if (queue && last_fence_value)
            gst_d3d12_cmd_queue_fence_wait(queue, last_fence_value);
    }
};

} // namespace

struct D3D12SurfaceSharing::Impl {
    cl_context cl_ctx = nullptr;
    cl_device_id cl_dev = nullptr;
    cl_command_queue cl_queue = nullptr;
    clEnqueueAcquireExternalMemObjectsKHR_fn acquire = nullptr;
    clEnqueueReleaseExternalMemObjectsKHR_fn release = nullptr;

    std::mutex mutex;
    std::unordered_map<ID3D12Resource *, TextureCacheEntry> cache;
    std::map<ID3D12Device *, CopyRecorder> recorders;

    explicit Impl(cl_context ctx) {
        if (!ctx)
            throw std::invalid_argument("D3D12SurfaceSharing: null cl_context");

        clRetainContext(ctx);
        cl_ctx = ctx;

        check_cl(clGetContextInfo(cl_ctx, CL_CONTEXT_DEVICES, sizeof(cl_dev), &cl_dev, nullptr),
                 "clGetContextInfo(CL_CONTEXT_DEVICES)");
        cl_int err = CL_SUCCESS;
        cl_queue = CreateInteropCommandQueue(cl_ctx, cl_dev, &err);
        check_cl(err, "clCreateCommandQueue");

        cl_platform_id platform = nullptr;
        check_cl(clGetDeviceInfo(cl_dev, CL_DEVICE_PLATFORM, sizeof(platform), &platform, nullptr),
                 "clGetDeviceInfo(CL_DEVICE_PLATFORM)");

        // The win32 sub-extension string is not always advertised even when the
        // handle type is importable, so check the handle-type list instead.
        cl_external_memory_handle_type_khr htypes[16];
        size_t ret = 0;
        err = clGetDeviceInfo(cl_dev, CL_DEVICE_EXTERNAL_MEMORY_IMPORT_HANDLE_TYPES_KHR, sizeof(htypes), htypes, &ret);
        bool has_win32 = false;
        if (err == CL_SUCCESS) {
            for (size_t i = 0; i < ret / sizeof(htypes[0]); ++i)
                if (htypes[i] == CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR)
                    has_win32 = true;
        }
        if (!has_win32)
            throw std::runtime_error("D3D12SurfaceSharing: OpenCL device cannot import OPAQUE_WIN32 external memory "
                                     "(cl_khr_external_memory_win32)");

        acquire = reinterpret_cast<clEnqueueAcquireExternalMemObjectsKHR_fn>(
            clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueAcquireExternalMemObjectsKHR"));
        release = reinterpret_cast<clEnqueueReleaseExternalMemObjectsKHR_fn>(
            clGetExtensionFunctionAddressForPlatform(platform, "clEnqueueReleaseExternalMemObjectsKHR"));
        if (!acquire || !release)
            throw std::runtime_error("D3D12SurfaceSharing: cl_khr_external_memory entry points not available");

        GVA_INFO("D3D12SurfaceSharing: OpenCL external-memory (D3D12 shared buffer) interop initialized");
    }

    ~Impl() {
        recorders.clear(); // drain outstanding copies before the buffers they read/write go away
        cache.clear();
        if (cl_queue)
            clReleaseCommandQueue(cl_queue);
        if (cl_ctx)
            clReleaseContext(cl_ctx);
    }

    CopyRecorder &get_recorder(GstD3D12Device *gst_device) {
        auto *dev = gst_d3d12_device_get_device_handle(gst_device);
        auto it = recorders.find(dev);
        if (it != recorders.end())
            return it->second;
        auto &rec = recorders[dev];
        rec.init(gst_device);
        return rec;
    }

    TextureCacheEntry &get_or_create(GstD3D12Device *gst_device, ID3D12Resource *texture, uint32_t subresource,
                                     uint32_t width, uint32_t height) {
        auto &entry = cache[texture];
        if (entry.cl_buf)
            return entry;

        auto *dev = gst_d3d12_device_get_device_handle(gst_device);
        D3D12_RESOURCE_DESC tex_desc = texture->GetDesc();
        UINT64 total = 0;
        dev->GetCopyableFootprints(&tex_desc, subresource, 1, 0, &entry.footprint, &entry.num_rows, nullptr, &total);
        entry.footprint.Offset = 0;
        entry.width = width;
        entry.height = height;
        const size_t buf_size = static_cast<size_t>(entry.footprint.Footprint.RowPitch) * entry.num_rows;

        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC bd = {};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = buf_size;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        check_hr(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &bd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                              IID_PPV_ARGS(&entry.shared_buf)),
                 "CreateCommittedResource(shared buffer)");
        check_hr(dev->CreateSharedHandle(entry.shared_buf.Get(), nullptr, GENERIC_ALL, nullptr, &entry.nt_handle),
                 "CreateSharedHandle(shared buffer)");

        cl_mem_properties props[] = {static_cast<cl_mem_properties>(CL_EXTERNAL_MEMORY_HANDLE_OPAQUE_WIN32_KHR),
                                     reinterpret_cast<cl_mem_properties>(entry.nt_handle),
                                     static_cast<cl_mem_properties>(CL_MEM_DEVICE_HANDLE_LIST_KHR),
                                     reinterpret_cast<cl_mem_properties>(cl_dev),
                                     static_cast<cl_mem_properties>(CL_MEM_DEVICE_HANDLE_LIST_END_KHR),
                                     0};
        cl_int err = CL_SUCCESS;
        entry.cl_buf = clCreateBufferWithProperties(cl_ctx, props, CL_MEM_READ_WRITE, buf_size, nullptr, &err);
        if (err != CL_SUCCESS) {
            cache.erase(texture);
            check_cl(err, "clCreateBufferWithProperties(external memory)");
        }

        const size_t packed_pitch = static_cast<size_t>(width) * 4;
        entry.compact = clCreateBuffer(cl_ctx, CL_MEM_READ_WRITE, packed_pitch * height, nullptr, &err);
        if (err != CL_SUCCESS) {
            cache.erase(texture);
            check_cl(err, "clCreateBuffer(compact)");
        }

        GVA_INFO("D3D12SurfaceSharing: imported shared buffer for texture %p (%ux%u, row_pitch=%u)",
                 static_cast<void *>(texture), width, height, entry.footprint.Footprint.RowPitch);
        return entry;
    }
};

D3D12SurfaceSharing::D3D12SurfaceSharing(cl_context cl_ctx) : _impl(std::make_unique<Impl>(cl_ctx)) {
}

D3D12SurfaceSharing::~D3D12SurfaceSharing() = default;

cl_mem D3D12SurfaceSharing::create_cl_buffer(GstD3D12Device *device, ID3D12Resource *texture, uint32_t subresource,
                                             uint32_t width, uint32_t height, ID3D12Fence *wait_fence,
                                             uint64_t wait_value) {
    if (!device || !texture)
        throw std::invalid_argument("D3D12SurfaceSharing::create_cl_buffer: null device/texture");

    std::lock_guard lk(_impl->mutex);

    auto &entry = _impl->get_or_create(device, texture, subresource, width, height);
    auto &rec = _impl->get_recorder(device);

    // Texture -> shared buffer on the COPY queue, ordered after the converter's DIRECT
    // work through a GPU-side fence wait. ALLOW_SIMULTANEOUS_ACCESS textures decay to
    // COMMON between submissions, so no explicit barrier is needed.
    check_hr(rec.alloc->Reset(), "COPY allocator Reset");
    check_hr(rec.cl->Reset(rec.alloc.Get(), nullptr), "COPY command list Reset");

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = entry.shared_buf.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = entry.footprint;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = subresource;
    rec.cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    check_hr(rec.cl->Close(), "COPY command list Close");

    ID3D12CommandList *lists[] = {rec.cl.Get()};
    ID3D12Fence *waits[] = {wait_fence};
    UINT64 wait_values[] = {wait_value};
    guint64 copy_fence_value = 0;
    check_hr(gst_d3d12_cmd_queue_execute_command_lists_full(rec.queue, wait_fence ? 1 : 0, waits, wait_values, 1, lists,
                                                            &copy_fence_value),
             "ExecuteCommandLists(COPY)");
    rec.last_fence_value = copy_fence_value;

    // The OpenCL side has no view of the D3D12 fence, so wait for the copy on the CPU
    // before handing the buffer over.
    check_hr(gst_d3d12_cmd_queue_fence_wait(rec.queue, copy_fence_value), "COPY fence wait");

    cl_int err = _impl->acquire(_impl->cl_queue, 1, &entry.cl_buf, 0, nullptr, nullptr);
    check_cl(err, "clEnqueueAcquireExternalMemObjectsKHR");

    // OpenVINO reads the buffer after the release below, and an external memory object
    // must not be used outside an acquire/release pair: hand over a packed regular copy,
    // even when the row pitch is already packed. A plain buffer copy is much cheaper
    // than a rect copy, so the rect copy only strips row padding.
    const size_t packed_pitch = static_cast<size_t>(width) * 4;
    if (entry.footprint.Footprint.RowPitch == packed_pitch) {
        err = clEnqueueCopyBuffer(_impl->cl_queue, entry.cl_buf, entry.compact, 0, 0, packed_pitch * height, 0, nullptr,
                                  nullptr);
    } else {
        const size_t zero[3] = {0, 0, 0};
        const size_t region[3] = {packed_pitch, height, 1};
        err = clEnqueueCopyBufferRect(_impl->cl_queue, entry.cl_buf, entry.compact, zero, zero, region,
                                      entry.footprint.Footprint.RowPitch, 0, packed_pitch, 0, 0, nullptr, nullptr);
    }
    // Release even if the copy failed, so the next acquire of this buffer is valid.
    const cl_int release_err = _impl->release(_impl->cl_queue, 1, &entry.cl_buf, 0, nullptr, nullptr);
    check_cl(err, "clEnqueueCopyBuffer(Rect)");
    check_cl(release_err, "clEnqueueReleaseExternalMemObjectsKHR");

    // Orders the acquire/copy before OV's own queue reads the buffer.
    check_cl(clFinish(_impl->cl_queue), "clFinish");

    return entry.compact;
}
