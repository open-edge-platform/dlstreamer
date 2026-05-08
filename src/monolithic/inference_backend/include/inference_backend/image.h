/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/context.h"
#include "dlstreamer/frame.h"
#include "dlstreamer/image_info.h"

#include <cstdint>
#include <memory>
#include <type_traits>

#ifdef _WIN32
#include <d3d11.h>
#endif

using VaApiDisplayPtr = dlstreamer::ContextPtr;

namespace {
template <int a, int b, int c, int d>
struct fourcc {
    enum { code = (a) | (b << 8) | (c << 16) | (d << 24) };
};

} // namespace

namespace InferenceBackend {

enum class MemoryType { ANY = 0, SYSTEM = 1, DMA_BUFFER = 2, VAAPI = 3, USM_DEVICE_POINTER = 4, D3D11 = 5 };

enum FourCC {
    FOURCC_RGBP_F32 = 0x07282024,
    FOURCC_NV12 = static_cast<int>(dlstreamer::ImageFormat::NV12),
    FOURCC_BGRA = fourcc<'B', 'G', 'R', 'A'>::code,
    FOURCC_BGRX = static_cast<int>(dlstreamer::ImageFormat::BGRX),
    FOURCC_BGRP = static_cast<int>(dlstreamer::ImageFormat::BGRP),
    FOURCC_BGR = static_cast<int>(dlstreamer::ImageFormat::BGR),
    FOURCC_RGBA = fourcc<'R', 'G', 'B', 'A'>::code,
    FOURCC_RGBX = static_cast<int>(dlstreamer::ImageFormat::RGBX),
    FOURCC_RGB = static_cast<int>(dlstreamer::ImageFormat::RGB),
    FOURCC_RGBP = static_cast<int>(dlstreamer::ImageFormat::RGBP),
    FOURCC_I420 = static_cast<int>(dlstreamer::ImageFormat::I420),
    FOURCC_YUV = fourcc<'Y', 'U', 'V', ' '>::code
};

template <typename T>
struct Rectangle {
    static_assert(std::is_floating_point<T>::value or std::is_integral<T>::value,
                  "struct Rectangle can only be instantiated with numeric type types");

    T x = 0;
    T y = 0;
    T width = 0;
    T height = 0;

    Rectangle() = default;
    Rectangle(T x, T y, T width, T height) : x(x), y(y), width(width), height(height) {
    }
};

struct Image {
    MemoryType type = MemoryType::ANY;
    static const uint32_t MAX_PLANES_NUMBER = 4;

    union {
        uint8_t *planes[MAX_PLANES_NUMBER]; // if type==SYSTEM
        struct {                            // if type==VAAPI
            uint32_t va_surface_id;
            void *va_display;
        };
        struct { // if type==D3D11
#ifdef _WIN32
            struct ID3D11Texture2D *d3d11_texture;
#else
            void *d3d11_texture;
#endif
            uint32_t d3d11_subresource_index; // Array slice index for texture arrays
            void *gst_d3d11_device;           // GstD3D11Device* from source pipeline
        };
    };
    int dma_fd = -1; // if type==DMA_BUFFER

    int format = 0; // FourCC
    uint64_t drm_format_modifier = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t size = 0;
    uint32_t stride[MAX_PLANES_NUMBER] = {0, 0, 0, 0};
    uint32_t offsets[MAX_PLANES_NUMBER] = {0, 0, 0, 0};
    Rectangle<uint32_t> rect;

    // Filled in and used by the USMBufferMapper.
    void *map_context = nullptr;

    Image() = default;
};

using ImagePtr = std::shared_ptr<Image>;

// Map DMA/VAAPI image into system memory
class ImageMap {
  public:
    virtual Image Map(const Image &image) = 0;
    virtual void Unmap() = 0;

    static ImageMap *Create(MemoryType type);
    virtual ~ImageMap() = default;
};

} // namespace InferenceBackend
