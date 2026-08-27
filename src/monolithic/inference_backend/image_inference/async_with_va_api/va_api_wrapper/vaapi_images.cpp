/*******************************************************************************
 * Copyright (C) 2019-2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "vaapi_images.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/dma-heap.h>

using namespace InferenceBackend;

namespace {

VASurfaceID CreateVASurface(VaDpyWrapper display, uint32_t width, uint32_t height, int pixel_format, int rt_format) {
    VASurfaceAttrib surface_attrib;
    surface_attrib.type = VASurfaceAttribPixelFormat;
    surface_attrib.flags = VA_SURFACE_ATTRIB_SETTABLE;
    surface_attrib.value.type = VAGenericValueTypeInteger;
    surface_attrib.value.value.i = pixel_format;

    VASurfaceID va_surface_id;
    VA_CALL(display.drvVtable().vaCreateSurfaces2(display.drvCtx(), rt_format, width, height, &va_surface_id, 1,
                                                  &surface_attrib, 1))
    return va_surface_id;
}

// Returns dma-buf fd on success, -1 on failure (e.g. no permission).
int AllocateDmaBuf(size_t size) {
    int heap_fd = open("/dev/dma_heap/system", O_RDWR);
    if (heap_fd < 0)
        return -1;

    struct dma_heap_allocation_data alloc_data = {};
    alloc_data.len = size;
    alloc_data.fd_flags = O_CLOEXEC | O_RDWR;
    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) < 0) {
        close(heap_fd);
        return -1;
    }
    close(heap_fd);
    return static_cast<int>(alloc_data.fd);
}

VASurfaceID CreateVASurfaceWithDmaBuf(VaDpyWrapper display, uint32_t width, uint32_t height, int pixel_format,
                                      int rt_format, int dma_buf_fd) {
    VASurfaceAttribExternalBuffers ext_buf = {};
    ext_buf.pixel_format = pixel_format;
    ext_buf.width = width;
    ext_buf.height = height;
    ext_buf.num_planes = 3;
    ext_buf.num_buffers = 1;
    uintptr_t fd_handle = static_cast<uintptr_t>(dma_buf_fd);
    ext_buf.buffers = &fd_handle;
    ext_buf.data_size = width * height * 3; // RGBP/BGRP: 3 planes, each W*H

    ext_buf.pitches[0] = width;
    ext_buf.offsets[0] = 0;
    ext_buf.pitches[1] = width;
    ext_buf.offsets[1] = width * height;
    ext_buf.pitches[2] = width;
    ext_buf.offsets[2] = 2 * width * height;

    VASurfaceAttrib attribs[3];

    attribs[0].type = VASurfaceAttribPixelFormat;
    attribs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attribs[0].value.type = VAGenericValueTypeInteger;
    attribs[0].value.value.i = pixel_format;

    attribs[1].type = VASurfaceAttribMemoryType;
    attribs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attribs[1].value.type = VAGenericValueTypeInteger;
    attribs[1].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;

    attribs[2].type = VASurfaceAttribExternalBufferDescriptor;
    attribs[2].flags = VA_SURFACE_ATTRIB_SETTABLE;
    attribs[2].value.type = VAGenericValueTypePointer;
    attribs[2].value.value.p = &ext_buf;

    VASurfaceID va_surface_id;
    VA_CALL(display.drvVtable().vaCreateSurfaces2(display.drvCtx(), rt_format, width, height, &va_surface_id, 1,
                                                  attribs, 3))
    return va_surface_id;
}

struct Format {
    uint32_t va_fourcc;
    InferenceBackend::FourCC ib_fourcc;
};

// This structure contains formats supported by software processing, in order of priority.
constexpr Format possible_formats[] = {
    {VA_FOURCC_BGRA, InferenceBackend::FourCC::FOURCC_BGRA}, {VA_FOURCC_BGRX, InferenceBackend::FourCC::FOURCC_BGRX},
    {VA_FOURCC_RGBA, InferenceBackend::FourCC::FOURCC_RGBA}, {VA_FOURCC_RGBX, InferenceBackend::FourCC::FOURCC_RGBX},
    {VA_FOURCC_I420, InferenceBackend::FourCC::FOURCC_I420}, {VA_FOURCC_NV12, InferenceBackend::FourCC::FOURCC_NV12}};

std::string FourccName(int code) {
    const char c1 = (code & (0x000000ff << 24)) >> 24;
    const char c2 = (code & (0x000000ff << 16)) >> 16;
    const char c3 = (code & (0x000000ff << 8)) >> 8;
    const char c4 = code & 0x000000ff;

    return {c4, c3, c2, c1};
}

} // namespace

VaApiImage::VaApiImage() {
    image.va_surface_id = VA_INVALID_SURFACE;
    image.va_display = nullptr;
}

VaApiImage::VaApiImage(VaApiContext *context_, uint32_t width, uint32_t height, int pixel_format,
                       MemoryType memory_type, uint32_t scaling_flgs /*= VA_FILTER_SCALING_DEFAULT*/) {
    if (!context_)
        throw std::invalid_argument("Invalid Vaapi context object");

    context = context_;
    image.type = memory_type;
    image.width = width;
    image.height = height;
    image.format = pixel_format;
    image.va_display = context->DisplayRaw();

    if (memory_type == MemoryType::DMA_BUFFER) {
        size_t buf_size = static_cast<size_t>(width) * height * 3; // RGBP/BGRP
        dma_buf_fd = AllocateDmaBuf(buf_size);
        if (dma_buf_fd >= 0) {
            // DRM_PRIME import requires matching RT format; RGBP/BGRP need VA_RT_FORMAT_RGBP
            image.va_surface_id = CreateVASurfaceWithDmaBuf(context->Display(), width, height, pixel_format,
                                                            VA_RT_FORMAT_RGBP, dma_buf_fd);
            image.dma_fd = dma_buf_fd;
        } else {
            GVA_WARNING("DMA-BUF allocation failed (no access to /dev/dma_heap/system?), "
                        "falling back to VAAPI_SYSTEM path (GPU->CPU copy)");
            image.type = MemoryType::SYSTEM;
            image.va_surface_id =
                CreateVASurface(context->Display(), width, height, pixel_format, context_->RTFormat());
            image_map = std::unique_ptr<ImageMap>(ImageMap::Create(MemoryType::SYSTEM));
            completed = true;
            scaling_flags = scaling_flgs;
            return;
        }
    } else {
        image.va_surface_id = CreateVASurface(context->Display(), width, height, pixel_format, context_->RTFormat());
    }

    image_map = std::unique_ptr<ImageMap>(ImageMap::Create(memory_type));
    completed = true;
    scaling_flags = scaling_flgs;
}

VaApiImage::~VaApiImage() {
    if (image.va_surface_id == VA_INVALID_ID)
        return;

    try {
        auto dpy = VaDpyWrapper::fromHandle(image.va_display);
        VA_CALL(dpy.drvVtable().vaDestroySurfaces(dpy.drvCtx(), &image.va_surface_id, 1));
    } catch (const std::exception &e) {
        GVA_WARNING("VA surface destroying failed: %s", e.what());
    }

    if (dma_buf_fd >= 0) {
        close(dma_buf_fd);
        dma_buf_fd = -1;
    }
}

void VaApiImage::Unmap() {
    image_map->Unmap();
}

Image VaApiImage::Map() {
    return image_map->Map(image);
}

VaApiImagePool::VaApiImagePool(VaApiContext *context, SizeParams size_params, ImageInfo info) {
    if (!context)
        throw std::invalid_argument("VaApiContext is nullptr");

    if (size_params.size() == 0)
        throw std::invalid_argument("size_params can't be zero");

    if (!context->IsPixelFormatSupported(info.format)) {
        std::string msg = "Unsupported requested pixel format " + FourccName(info.format) + ". ";
        switch (info.memory_type) {
        case InferenceBackend::MemoryType::SYSTEM: {
            // In the case when the system memory is requested, we can choose the supported format and do software color
            // conversion after.
            bool is_set = false;
            for (auto format : possible_formats)
                if (context->IsPixelFormatSupported(format.va_fourcc)) {
                    msg += "Using a supported format " + FourccName(format.va_fourcc) + ".";
                    info.format = format.ib_fourcc;
                    is_set = true;
                    break;
                }
            if (not is_set)
                throw std::runtime_error(msg + "Could not set the other pixel format, none are supported.");
            else
                GVA_WARNING("%s", msg.c_str());
            break;
        }
        // In the case when the vaapi memory is requested, we cannot do software color conversion after.
        case InferenceBackend::MemoryType::VAAPI:
        case InferenceBackend::MemoryType::DMA_BUFFER:
            throw std::runtime_error("Could not set the pixel format for vaapi memory. " + msg);
        default:
            throw std::runtime_error(msg + "Memory type is not supported to select an alternative pixel format.");
        }
    }

    GVA_INFO("VA-API image pool size: default=%u, fast=%u", size_params.num_default, size_params.num_fast);

    _images.reserve(size_params.size());
    for (size_t i = 0; i < size_params.size(); i++) {
        const uint32_t scaling_method = i < size_params.num_fast ? VA_FILTER_SCALING_FAST : VA_FILTER_SCALING_DEFAULT;
        _images.push_back(std::unique_ptr<VaApiImage>(
            new VaApiImage(context, info.width, info.height, info.format, info.memory_type, scaling_method)));
    }
}

VaApiImage *VaApiImagePool::AcquireBuffer() {
    std::unique_lock<std::mutex> lock(_free_images_mutex);
    for (;;) {
        for (auto &image : _images) {
            if (image->completed) {
                image->completed = false;
                return image.get();
            }
        }
        _free_image_condition_variable.wait(lock);
    }
}

void VaApiImagePool::ReleaseBuffer(VaApiImage *image) {
    if (!image)
        throw std::runtime_error("Received VA-API image is null");

    image->completed = true;
    _free_image_condition_variable.notify_one();
}

void VaApiImagePool::Flush() {
    std::unique_lock<std::mutex> lock(_free_images_mutex);
    for (auto &image : _images) {
        if (!image->completed)
            image->sync.wait();
    }
}
