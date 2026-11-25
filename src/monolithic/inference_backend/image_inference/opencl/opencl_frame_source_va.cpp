/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// VA surfaces, imported plane by plane through cl_intel_va_api_media_sharing. NV12 DMA-BUFs are imported into a VA
// surface of the display first.

#include "opencl_frame_source.h"
#include "opencl_utils.h"

#include "inference_backend/logger.h"
#include "vaapi_utils.h"

#include <CL/cl_va_api_media_sharing_intel.h>
#include <va/va_drmcommon.h>

#include <climits>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace InferenceBackend {

namespace {

// Kernel driver of the DRM device behind the VA display ("i915", "xe", ...), empty if unknown.
std::string DrmDriverName(VADisplay display) {
    try {
        auto *drm = static_cast<drm_state *>(VaDpyWrapper::fromHandle(display).drvCtx()->drm_state);
        struct stat st = {};
        if (!drm || drm->fd < 0 || fstat(drm->fd, &st) != 0 || !S_ISCHR(st.st_mode))
            return {};
        const std::string link = "/sys/dev/char/" + std::to_string(major(st.st_rdev)) + ":" +
                                 std::to_string(minor(st.st_rdev)) + "/device/driver";
        char target[PATH_MAX] = {};
        const ssize_t n = readlink(link.c_str(), target, sizeof(target) - 1);
        if (n <= 0)
            return {};
        const std::string path(target, n);
        return path.substr(path.find_last_of('/') + 1);
    } catch (const std::exception &) {
        return {};
    }
}

class VaFrameSource : public OpenclFrameSource {
  public:
    VaFrameSource(cl_context context, VADisplay display) : context_(context), display_(display) {
        if (!context_ || !display_)
            throw std::invalid_argument("OpenCL pre-processing: VA frames need a VA display");
        const cl_platform_id platform = opencl::DevicePlatform(opencl::ContextDevice(context_));
        create_from_va_ = opencl::GetExtensionFunction<clCreateFromVA_APIMediaSurfaceINTEL_fn>(
            platform, "clCreateFromVA_APIMediaSurfaceINTEL");
        acquire_ = opencl::GetExtensionFunction<clEnqueueAcquireVA_APIMediaSurfacesINTEL_fn>(
            platform, "clEnqueueAcquireVA_APIMediaSurfacesINTEL");
        release_ = opencl::GetExtensionFunction<clEnqueueReleaseVA_APIMediaSurfacesINTEL_fn>(
            platform, "clEnqueueReleaseVA_APIMediaSurfacesINTEL");
        if (!create_from_va_ || !acquire_ || !release_)
            throw std::runtime_error("OpenCL pre-processing: cl_intel_va_api_media_sharing is not available");

        // On i915 an import must not outlive its frame: it keeps the surface attached to the GPU context, which
        // breaks CPU maps of the surface elsewhere. There only the current frame's import is kept.
        const std::string driver = DrmDriverName(display_);
        cache_imports_ = driver != "i915";
        GVA_INFO("OpenCL pre-processing: VA surfaces of a '%s' device, imports %s", driver.c_str(),
                 cache_imports_ ? "cached per surface" : "per frame");
    }

    OpenclSourcePlanesPtr GetPlanes(const ImagePtr &image, cl_command_queue) override {
        if (!image)
            throw std::invalid_argument("OpenCL pre-processing: null frame");
        if (image->format != FourCC::FOURCC_NV12)
            throw std::invalid_argument("OpenCL pre-processing: VA surfaces and DMA-BUFs must be NV12");
        if (image->type == MemoryType::VAAPI)
            return GetSurfacePlanes(*image);
        if (image->type == MemoryType::DMA_BUFFER)
            return GetDmaBufPlanes(image);
        throw std::invalid_argument("OpenCL pre-processing: expected a VA surface or a DMA-BUF");
    }

  private:
    OpenclSourcePlanesPtr GetSurfacePlanes(const Image &image) {
        if (image.va_display != display_)
            throw std::runtime_error("OpenCL pre-processing: the frame comes from another VA display than the one "
                                     "of the pre-processing (frames decoded on another GPU are not supported)");
        const VASurfaceID surface = image.va_surface_id;
        if (auto it = imports_.find(surface); it != imports_.end())
            return it->second;
        if (!cache_imports_)
            imports_.clear(); // still alive while a batch uses them
        return imports_.emplace(surface, Import(surface, {})).first->second;
    }

    OpenclSourcePlanesPtr GetDmaBufPlanes(const ImagePtr &image) {
        if (auto last = last_dmabuf_image_.lock(); last && last.get() == image.get() && last_dmabuf_planes_)
            return last_dmabuf_planes_;

        // One NV12 layer of two planes in one object, with its modifier (decoder and VPP output is usually tiled).
        const Image &src = *image;
        VADRMPRIMESurfaceDescriptor desc = {};
        desc.fourcc = VA_FOURCC_NV12;
        desc.width = src.width;
        desc.height = src.height;
        desc.num_objects = 1;
        desc.objects[0].fd = src.dma_fd;
        desc.objects[0].size = src.size ? src.size : static_cast<uint32_t>(lseek(src.dma_fd, 0, SEEK_END));
        desc.objects[0].drm_format_modifier = src.drm_format_modifier;
        desc.num_layers = 1;
        desc.layers[0].drm_format = VA_FOURCC_NV12; // DRM_FORMAT_NV12 has the same code
        desc.layers[0].num_planes = 2;
        for (uint32_t i = 0; i < 2; i++) {
            desc.layers[0].object_index[i] = 0;
            desc.layers[0].offset[i] = src.offsets[i];
            desc.layers[0].pitch[i] = src.stride[i];
        }
        VASurfaceAttrib attribs[2] = {};
        attribs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attribs[0].type = VASurfaceAttribMemoryType;
        attribs[0].value.type = VAGenericValueTypeInteger;
        attribs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2;
        attribs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attribs[1].type = VASurfaceAttribExternalBufferDescriptor;
        attribs[1].value.type = VAGenericValueTypePointer;
        attribs[1].value.value.p = &desc;

        GVA_DEBUG("OpenCL pre-processing: DMA-BUF fd %d %ux%u size %u modifier 0x%llx, planes at %u/%u, pitches %u/%u",
                  src.dma_fd, src.width, src.height, desc.objects[0].size,
                  static_cast<unsigned long long>(src.drm_format_modifier), src.offsets[0], src.offsets[1],
                  src.stride[0], src.stride[1]);
        const VaDpyWrapper display = VaDpyWrapper::fromHandle(display_);
        VASurfaceID surface = VA_INVALID_SURFACE;
        VA_CALL(display.drvVtable().vaCreateSurfaces2(display.drvCtx(), VA_RT_FORMAT_YUV420, src.width, src.height,
                                                      &surface, 1, attribs, 2));
        // Destroyed with the planes, once the kernels reading it have completed.
        auto destroy = [display, surface]() mutable {
            display.drvVtable().vaDestroySurfaces(display.drvCtx(), &surface, 1);
        };
        OpenclSourcePlanesPtr planes;
        try {
            planes = Import(surface, destroy);
        } catch (...) {
            destroy();
            throw;
        }
        last_dmabuf_image_ = image;
        last_dmabuf_planes_ = planes;
        return planes;
    }

    OpenclSourcePlanesPtr Import(VASurfaceID surface, std::function<void()> on_destroy) {
        auto planes = std::make_shared<OpenclSourcePlanes>();
        planes->layout = OpenclSourcePlanes::Layout::NV12;
        for (cl_uint plane = 0; plane < 2; ++plane) {
            cl_int err = CL_SUCCESS;
            VASurfaceID id = surface;
            planes->planes[plane] = create_from_va_(context_, CL_MEM_READ_ONLY, &id, plane, &err);
            opencl::Check(err == CL_SUCCESS && !planes->planes[plane] ? CL_INVALID_MEM_OBJECT : err,
                          "clCreateFromVA_APIMediaSurfaceINTEL");
            planes->num_planes = plane + 1;
        }
        const auto acquire = acquire_;
        const auto release = release_;
        const cl_mem mems[2] = {planes->planes[0], planes->planes[1]};
        planes->acquire = [acquire, mems](cl_command_queue queue) {
            opencl::Check(acquire(queue, 2, mems, 0, nullptr, nullptr), "clEnqueueAcquireVA_APIMediaSurfacesINTEL");
        };
        planes->release = [release, mems](cl_command_queue queue) {
            opencl::Check(release(queue, 2, mems, 0, nullptr, nullptr), "clEnqueueReleaseVA_APIMediaSurfacesINTEL");
        };
        // Only once the import has succeeded: on failure the caller cleans up.
        planes->on_destroy = std::move(on_destroy);
        return planes;
    }

    cl_context context_;
    VADisplay display_;
    clCreateFromVA_APIMediaSurfaceINTEL_fn create_from_va_ = nullptr;
    clEnqueueAcquireVA_APIMediaSurfacesINTEL_fn acquire_ = nullptr;
    clEnqueueReleaseVA_APIMediaSurfacesINTEL_fn release_ = nullptr;

    // Imports of the decoder's surfaces: its pool is fixed, so they are kept (see cache_imports_).
    bool cache_imports_ = true;
    std::unordered_map<VASurfaceID, OpenclSourcePlanesPtr> imports_;

    // The last DMA-BUF frame imported, shared by its ROIs. Only a weak reference to the frame.
    std::weak_ptr<Image> last_dmabuf_image_;
    OpenclSourcePlanesPtr last_dmabuf_planes_;
};

} // namespace

std::unique_ptr<OpenclFrameSource> CreateOpenclVaFrameSource(cl_context context, void *va_display) {
    return std::make_unique<VaFrameSource>(context, static_cast<VADisplay>(va_display));
}

} // namespace InferenceBackend
