/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Shared D3D11 textures, imported through cl_khr_d3d11_sharing (cl_intel_d3d11_nv12_media_sharing for NV12). OpenCL
// reads only single textures created with D3D11_RESOURCE_MISC_SHARED correctly, which decoder textures are not:
// ImageInferenceAsyncOpenCL copies the frames into such textures first.

#include "opencl_frame_source.h"
#include "opencl_utils.h"

#include "dlstreamer/d3d11/device_lock.h"
#include "dlstreamer/d3d11/opencl_sharing.h"
#include "inference_backend/logger.h"

#ifndef GST_USE_UNSTABLE_API
#define GST_USE_UNSTABLE_API
#endif
#include <gst/d3d11/gstd3d11.h>

#include <d3d11.h>

#include <stdexcept>
#include <string>
#include <unordered_map>

namespace InferenceBackend {

namespace {

class D3D11FrameSource : public OpenclFrameSource {
  public:
    D3D11FrameSource(cl_context context, GstD3D11Device *device) : context_(context) {
        if (!context_ || !device)
            throw std::invalid_argument("OpenCL pre-processing: D3D11 frames need a D3D11 device");
        device_ = static_cast<GstD3D11Device *>(gst_object_ref(device));
        const cl_device_id cl_device = opencl::ContextDevice(context_);
        const dlstreamer::D3D11SharingFunctions sharing(opencl::DevicePlatform(cl_device));
        create_from_texture2d_ = sharing.create_from_texture2d;
        acquire_ = sharing.acquire;
        release_ = sharing.release;
        if (!create_from_texture2d_ || !acquire_ || !release_) {
            gst_object_unref(device_);
            throw std::runtime_error("OpenCL pre-processing: cl_khr_d3d11_sharing is not available");
        }
        if (!opencl::HasExtension(cl_device, "cl_intel_d3d11_nv12_media_sharing"))
            GVA_WARNING("OpenCL pre-processing: no cl_intel_d3d11_nv12_media_sharing, NV12 frames will fail");
    }

    ~D3D11FrameSource() override {
        // The imports go first: releasing them uses the device.
        imports_.clear();
        gst_object_unref(device_);
    }

    OpenclSourcePlanesPtr GetPlanes(const ImagePtr &image, cl_command_queue) override {
        if (!image || image->type != MemoryType::D3D11 || !image->d3d11_texture)
            throw std::invalid_argument("OpenCL pre-processing: expected a D3D11 texture");
        ID3D11Texture2D *texture = image->d3d11_texture;
        // The ROIs of a frame come one after another, with its texture.
        if (texture != last_texture_) {
            last_texture_ = texture;
            if (++frames_ % kImportCheckPeriod == 0)
                DropIdleImports();
        }
        // Each import holds a reference to its texture, so a key is not reused by another texture while cached.
        auto it = imports_.find(texture);
        if (it == imports_.end())
            it = imports_.emplace(texture, CachedImport{Import(texture)}).first;
        it->second.last_used = frames_;
        return it->second.planes;
    }

  private:
    // An import not used by the last kImportIdleFrames frames is dropped, which releases its texture: it went back to
    // a pool that is gone (one per frame size, dropped when idle), or the decoder whose texture was read in place
    // recreated its pool. As ImageInferenceAsyncOpenCL::kPoolIdleFrames.
    static constexpr uint64_t kImportIdleFrames = 1024;
    static constexpr uint64_t kImportCheckPeriod = 64;

    struct CachedImport {
        OpenclSourcePlanesPtr planes;
        uint64_t last_used = 0;
    };

    void DropIdleImports() {
        size_t dropped = 0;
        for (auto it = imports_.begin(); it != imports_.end();) {
            if (frames_ - it->second.last_used <= kImportIdleFrames) {
                ++it;
                continue;
            }
            // Batches still reading the planes keep them until their kernels have completed.
            it = imports_.erase(it);
            ++dropped;
        }
        if (dropped)
            GVA_INFO("OpenCL pre-processing: dropped %zu D3D11 imports unused for %llu frames, %zu left", dropped,
                     static_cast<unsigned long long>(kImportIdleFrames), imports_.size());
    }

    OpenclSourcePlanesPtr Import(ID3D11Texture2D *texture) {
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);
        if (!(desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) || desc.ArraySize != 1)
            throw std::invalid_argument("OpenCL pre-processing: the D3D11 texture is not a single shared texture");

        auto planes = std::make_shared<OpenclSourcePlanes>();
        switch (desc.Format) {
        case DXGI_FORMAT_NV12:
            planes->layout = OpenclSourcePlanes::Layout::NV12;
            planes->num_planes = 2;
            break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
            // Imported with the texture's channel order, so the kernel reads R, G, B either way.
            planes->layout = OpenclSourcePlanes::Layout::RGBX;
            planes->num_planes = 1;
            break;
        default:
            throw std::invalid_argument("OpenCL pre-processing: unsupported D3D11 texture format " +
                                        std::to_string(desc.Format));
        }

        // Released under the device lock, by on_destroy.
        planes->owns_planes = false;
        GstD3D11Device *device = device_;
        auto *planes_raw = planes.get();
        planes->on_destroy = [device, planes_raw]() {
            dlstreamer::D3D11DeviceLock lock(device);
            for (cl_uint i = 0; i < planes_raw->num_planes; ++i)
                if (planes_raw->planes[i])
                    clReleaseMemObject(planes_raw->planes[i]);
        };
        {
            // cl_intel_d3d11_nv12_media_sharing takes subresource 0 of a single NV12 texture as the Y plane and 1 as
            // the UV plane.
            dlstreamer::D3D11DeviceLock lock(device_);
            for (cl_uint plane = 0; plane < planes->num_planes; ++plane) {
                cl_int err = CL_SUCCESS;
                planes->planes[plane] = create_from_texture2d_(context_, CL_MEM_READ_ONLY, texture, plane, &err);
                opencl::Check(err == CL_SUCCESS && !planes->planes[plane] ? CL_INVALID_MEM_OBJECT : err,
                              "clCreateFromD3D11Texture2DKHR");
            }
        }

        const auto acquire = acquire_;
        const auto release = release_;
        const cl_uint count = planes->num_planes;
        const std::array<cl_mem, 3> mems = planes->planes;
        planes->acquire = [acquire, device, count, mems](cl_command_queue queue) {
            // With user sync this does not wait for the GPU, but it still goes through the D3D11 device.
            dlstreamer::D3D11DeviceLock lock(device);
            opencl::Check(acquire(queue, count, mems.data(), 0, nullptr, nullptr), "clEnqueueAcquireD3D11ObjectsKHR");
        };
        planes->release = [release, count, mems](cl_command_queue queue) {
            // Releasing a shared texture does not use the D3D11 context.
            opencl::Check(release(queue, count, mems.data(), 0, nullptr, nullptr), "clEnqueueReleaseD3D11ObjectsKHR");
        };
        GVA_DEBUG("OpenCL pre-processing: imported D3D11 texture %p (%ux%u, format %u)", static_cast<void *>(texture),
                  desc.Width, desc.Height, desc.Format);
        return planes;
    }

    cl_context context_;
    GstD3D11Device *device_ = nullptr;
    clCreateFromD3D11Texture2DKHR_fn create_from_texture2d_ = nullptr;
    clEnqueueAcquireD3D11ObjectsKHR_fn acquire_ = nullptr;
    clEnqueueReleaseD3D11ObjectsKHR_fn release_ = nullptr;
    std::unordered_map<ID3D11Texture2D *, CachedImport> imports_;
    // Frames seen, counted by texture changes, and the texture of the latest one. Only compared, never dereferenced.
    uint64_t frames_ = 0;
    ID3D11Texture2D *last_texture_ = nullptr;
};

} // namespace

std::unique_ptr<OpenclFrameSource> CreateOpenclD3D11FrameSource(cl_context context, void *gst_d3d11_device) {
    return std::make_unique<D3D11FrameSource>(context, static_cast<GstD3D11Device *>(gst_d3d11_device));
}

} // namespace InferenceBackend
