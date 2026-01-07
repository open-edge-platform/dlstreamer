/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/gst/mappers/gst_to_cpu.h"
#ifndef _WIN32
#include "dlstreamer/gst/mappers/gst_to_dma.h"
#include "dlstreamer/gst/mappers/gst_to_vaapi.h"
#else
#include "dlstreamer/gst/mappers/gst_to_d3d11.h"
#endif
#include "inference_backend/image.h"

namespace InferenceBackend {

// This class is used as shim between dlstreamer::Frame and InferenceBackend::Image
// The whole mapping process is handled by dlstreamer::Mapper
class BufferToImageMapper final {
  public:
    BufferToImageMapper(InferenceBackend::MemoryType memory_type, const GstVideoInfo *video_info,
                        dlstreamer::MemoryMapperPtr mapper)
        : _memory_type(memory_type), _video_info(video_info), _mapper(mapper) {
    }
    ~BufferToImageMapper() = default;

    InferenceBackend::MemoryType memoryType() const {
        return _memory_type;
    }

    InferenceBackend::ImagePtr map(GstBuffer *gst_buffer, GstMapFlags flags) {
        auto gbuffer = std::make_shared<dlstreamer::GSTFrame>(gst_buffer, _video_info);
        int mode = 0;
        if (flags & GST_MAP_READ)
            mode |= static_cast<int>(dlstreamer::AccessMode::Read);
        if (flags & GST_MAP_WRITE)
            mode |= static_cast<int>(dlstreamer::AccessMode::Write);

        dlstreamer::FramePtr buffer = _mapper->map(gbuffer, static_cast<dlstreamer::AccessMode>(mode));

        ImagePtr image;
        if (_memory_type == MemoryType::SYSTEM) {
            // For system memory, the buffer must remain mapped the lifetime of the image
            auto deleter = [buffer](Image *img) mutable {
                buffer.reset();
                delete img;
            };
            image = {new Image(), deleter};
        } else {
            // For VAAPI and DMA - we don't care because we only need handles.
            image = std::make_shared<Image>();
        }

        image->format = buffer->format();
        size_t size = 0;
        for (size_t i = 0; i < buffer->num_tensors(); i++) {
            auto tensor = buffer->tensor(i);
            auto &info = tensor->info();
            dlstreamer::ImageInfo image_info(info);
            if (_memory_type == MemoryType::SYSTEM || _memory_type == MemoryType::USM_DEVICE_POINTER)
                image->planes[i] = static_cast<uint8_t *>(tensor->data());
            else
                image->planes[i] = nullptr;
            image->offsets[i] = tensor->handle(dlstreamer::tensor::key::offset, 0);
            image->stride[i] = image_info.width_stride();
            size += info.nbytes();
        }
        auto tensor0 = buffer->tensor(0);
        auto info0 = tensor0->info();
        dlstreamer::ImageInfo image_info0(info0);
        image->width = image_info0.width();
        image->height = image_info0.height();
        image->size = size;
        image->type = _memory_type;
#ifndef _WIN32
        if (_memory_type == MemoryType::VAAPI) {
            image->va_surface_id = dlstreamer::ptr_cast<dlstreamer::VAAPITensor>(tensor0)->va_surface();
            image->va_display = tensor0->context()->handle(dlstreamer::VAAPIContext::key::va_display);
        };
        image->dma_fd = tensor0->handle(dlstreamer::tensor::key::dma_fd, 0);
        image->drm_format_modifier = tensor0->handle(dlstreamer::tensor::key::drm_modifier, 0);
#else
        if (_memory_type == MemoryType::D3D11) {
            image->d3d11_texture = dlstreamer::ptr_cast<dlstreamer::D3D11Tensor>(tensor0)->d3d11_texture();
            auto gst_d3d_device =
                static_cast<GstD3D11Device *>(tensor0->context()->handle(dlstreamer::D3D11Context::key::d3d_device));
            image->d3d11_device = reinterpret_cast<void *>(gst_d3d11_device_get_device_handle(gst_d3d_device));
        }
#endif
        return image;
    }

  private:
    InferenceBackend::MemoryType _memory_type;
    const GstVideoInfo *_video_info;
    dlstreamer::MemoryMapperPtr _mapper;
};

} // namespace InferenceBackend

class BufferMapperFactory {
  public:
    static dlstreamer::MemoryMapperPtr createMapper(InferenceBackend::MemoryType memory_type,
                                                    dlstreamer::ContextPtr output_context = nullptr) {
        switch (memory_type) {
        case InferenceBackend::MemoryType::SYSTEM:
            return std::make_shared<dlstreamer::MemoryMapperGSTToCPU>(nullptr, output_context);
#ifndef _WIN32
        case InferenceBackend::MemoryType::DMA_BUFFER:
            return std::make_shared<dlstreamer::MemoryMapperGSTToDMA>(nullptr, output_context);
        case InferenceBackend::MemoryType::VAAPI:
            return std::make_shared<dlstreamer::MemoryMapperGSTToVAAPI>(nullptr, output_context);
        case InferenceBackend::MemoryType::USM_DEVICE_POINTER:
            throw std::runtime_error("Not impemented");
#else
        case InferenceBackend::MemoryType::D3D11:
            return std::make_shared<dlstreamer::MemoryMapperGSTToD3D11>(nullptr, output_context);
#endif
        case InferenceBackend::MemoryType::ANY:
        default:
            break;
        }
        throw std::runtime_error("MemoryType not specified");
    }

    static std::unique_ptr<InferenceBackend::BufferToImageMapper>
    createMapper(InferenceBackend::MemoryType output_type, const GstVideoInfo *intput_video_info,
                 dlstreamer::ContextPtr output_context = nullptr) {
        dlstreamer::MemoryMapperPtr mapper = createMapper(output_type, output_context);
        return std::unique_ptr<InferenceBackend::BufferToImageMapper>(
            new InferenceBackend::BufferToImageMapper(output_type, intput_video_info, mapper));
    }

    BufferMapperFactory() = delete;
};
