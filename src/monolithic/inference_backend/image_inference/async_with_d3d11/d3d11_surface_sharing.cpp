/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "d3d11_surface_sharing.h"

#include "dlstreamer/d3d11/opencl_sharing.h"
#include "inference_backend/logger.h"
#include "inference_backend/opencl_interop_queue.h"

#include <d3d11.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

using namespace InferenceBackend;

namespace {

void check_cl(cl_int err, const char *msg) {
    if (err != CL_SUCCESS)
        throw std::runtime_error(std::string(msg) + " (cl_err=" + std::to_string(err) + ")");
}

struct TextureCacheEntry {
    cl_mem cl_image = nullptr;
    cl_mem cl_buf = nullptr;

    ~TextureCacheEntry() {
        if (cl_buf)
            clReleaseMemObject(cl_buf);
        if (cl_image)
            clReleaseMemObject(cl_image);
    }
};

} // namespace

struct D3D11SurfaceSharing::Impl {
    cl_context cl_ctx = nullptr;
    cl_device_id cl_dev = nullptr;
    cl_command_queue cl_queue = nullptr;
    clCreateFromD3D11Texture2DKHR_fn create_tex2d = nullptr;
    clEnqueueAcquireD3D11ObjectsKHR_fn acquire = nullptr;
    clEnqueueReleaseD3D11ObjectsKHR_fn release = nullptr;

    // Per-texture cache of cl_image + cl_buf wrappers. Keyed by D3D11 texture
    // pointer. The texture pool reuses a small set of textures across frames,
    // so this avoids re-creating OpenCL wrappers every frame.
    std::mutex mutex;
    std::unordered_map<ID3D11Texture2D *, TextureCacheEntry> cache;

    explicit Impl(cl_context ctx) {
        if (!ctx)
            throw std::invalid_argument("D3D11SurfaceSharing: null cl_context");

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

        const dlstreamer::D3D11SharingFunctions sharing(platform);
        create_tex2d = sharing.create_from_texture2d;
        acquire = sharing.acquire;
        release = sharing.release;
        if (!create_tex2d || !acquire || !release)
            throw std::runtime_error("cl_khr_d3d11_sharing extension not available");

        GVA_INFO("D3D11SurfaceSharing: OpenCL-D3D11 interop initialized");
    }

    ~Impl() {
        cache.clear(); // release cl_mem objects before releasing the queue/context
        if (cl_queue)
            clReleaseCommandQueue(cl_queue);
        if (cl_ctx)
            clReleaseContext(cl_ctx);
    }

    TextureCacheEntry &get_or_create(ID3D11Texture2D *texture, uint32_t width, uint32_t height) {
        auto &entry = cache[texture];
        if (entry.cl_image)
            return entry;

        cl_int err = CL_SUCCESS;
        entry.cl_image = create_tex2d(cl_ctx, CL_MEM_READ_WRITE, texture, 0, &err);
        check_cl(err, "clCreateFromD3D11Texture2DKHR");

        const size_t buf_size = static_cast<size_t>(width) * height * 4;
        entry.cl_buf = clCreateBuffer(cl_ctx, CL_MEM_READ_WRITE, buf_size, nullptr, &err);
        if (err != CL_SUCCESS) {
            clReleaseMemObject(entry.cl_image);
            entry.cl_image = nullptr;
            check_cl(err, "clCreateBuffer");
        }
        return entry;
    }
};

D3D11SurfaceSharing::D3D11SurfaceSharing(cl_context cl_ctx) : _impl(std::make_unique<Impl>(cl_ctx)) {
}

D3D11SurfaceSharing::~D3D11SurfaceSharing() = default;

cl_mem D3D11SurfaceSharing::create_cl_buffer(ID3D11Texture2D *texture, uint32_t width, uint32_t height) {
    if (!texture)
        throw std::invalid_argument("D3D11SurfaceSharing::create_cl_buffer: null texture");

    std::lock_guard lk(_impl->mutex);

    auto &entry = _impl->get_or_create(texture, width, height);

    // All operations on the same in-order queue, implicit serialization applies.
    // Only one clFinish at the end to sync with OV's inference queue.
    cl_int err = _impl->acquire(_impl->cl_queue, 1, &entry.cl_image, 0, nullptr, nullptr);
    check_cl(err, "clEnqueueAcquireD3D11ObjectsKHR");

    const size_t origin[3] = {0, 0, 0};
    const size_t region[3] = {width, height, 1};
    err = clEnqueueCopyImageToBuffer(_impl->cl_queue, entry.cl_image, entry.cl_buf, origin, region, 0, 0, nullptr,
                                     nullptr);
    // Release even if the copy failed: a texture left acquired fails every later acquire.
    const cl_int release_err = _impl->release(_impl->cl_queue, 1, &entry.cl_image, 0, nullptr, nullptr);
    check_cl(err, "clEnqueueCopyImageToBuffer");
    check_cl(release_err, "clEnqueueReleaseD3D11ObjectsKHR");

    check_cl(clFinish(_impl->cl_queue), "clFinish");

    return entry.cl_buf;
}
