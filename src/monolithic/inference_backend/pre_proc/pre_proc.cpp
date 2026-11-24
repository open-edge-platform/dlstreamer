/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "safe_arithmetic.hpp"
#include <functional>

#include "config.h"
#include "inference_backend/pre_proc.h"
#include "utils.h"

namespace InferenceBackend {

ImagePreprocessor *ImagePreprocessor::Create(ImagePreprocessorType type, const std::string custom_preproc_lib) {
    ImagePreprocessor *p = nullptr;
    switch (type) {
    case ImagePreprocessorType::OPENCV:
        p = CreatePreProcOpenCV(custom_preproc_lib);
        break;
    case ImagePreprocessorType::VAAPI_SYSTEM:
        p = CreatePreProcOpenCV(custom_preproc_lib);
        break;
    case ImagePreprocessorType::D3D11:
        p = CreatePreProcOpenCV(custom_preproc_lib);
        break;
    case ImagePreprocessorType::D3D12:
        p = CreatePreProcOpenCV(custom_preproc_lib);
        break;
    }
    if (p == nullptr)
        throw std::runtime_error("Failed to allocate Image preprocessor");
    return p;
}

Image ApplyCrop(const Image &src) {
    // GVA_DEBUG(__FUNCTION__);
    Image dst = src;
    int planes_count = Utils::GetPlanesCount(src.format);
    if (!src.rect.width && !src.rect.height) {
        for (int i = 0; i < planes_count; i++)
            dst.planes[i] = src.planes[i];
        return dst;
    }

    dst.rect = {};

    if (src.width <= src.rect.x or src.height <= src.rect.y)
        throw std::logic_error("ApplyCrop: Requested rectangle is out of image boundaries.");

    dst.width = std::min(src.rect.width, src.width - src.rect.x);
    dst.height = std::min(src.rect.height, src.height - src.rect.y);

    switch (src.format) {
    case InferenceBackend::FOURCC_NV12: {
        dst.planes[0] = src.planes[0] + src.rect.y * src.stride[0] + src.rect.x;
        dst.planes[1] = src.planes[1] + (src.rect.y / 2) * src.stride[1] + src.rect.x;
        break;
    }
    case InferenceBackend::FOURCC_I420: {
        dst.planes[0] = src.planes[0] + src.rect.y * src.stride[0] + src.rect.x;
        dst.planes[1] = src.planes[1] + (src.rect.y / 2) * src.stride[1] + (src.rect.x / 2);
        dst.planes[2] = src.planes[2] + (src.rect.y / 2) * src.stride[2] + (src.rect.x / 2);
        break;
    }
    case InferenceBackend::FOURCC_RGBP: {
        dst.planes[0] = src.planes[0] + src.rect.y * src.stride[0] + src.rect.x;
        dst.planes[1] = src.planes[1] + src.rect.y * src.stride[1] + src.rect.x;
        dst.planes[2] = src.planes[2] + src.rect.y * src.stride[2] + src.rect.x;
        break;
    }
    case InferenceBackend::FOURCC_BGR: {
        const uint32_t channels = 3;
        dst.planes[0] = src.planes[0] + src.rect.y * src.stride[0] + src.rect.x * channels;
        break;
    }
    case InferenceBackend::FOURCC_BGRX:
    case InferenceBackend::FOURCC_BGRA: {
        const uint32_t channels = 4;
        dst.planes[0] = src.planes[0] + src.rect.y * src.stride[0] + src.rect.x * channels;
        break;
    }
    default: {
        throw std::invalid_argument("Unsupported image format for crop");
        break;
    }
    }

    return dst;
}

bool ImagePreprocessor::needCustomImageConvert(const InputImageLayerDesc::Ptr &pre_proc_info) {
    return pre_proc_info && pre_proc_info->isDefined();
}

bool ImagePreprocessor::needPreProcessing(const Image &src, Image &dst) {
    if (src.format == dst.format and (src.format == FourCC::FOURCC_RGBP or src.format == FourCC::FOURCC_RGBP_F32) and
        src.width == dst.width and src.height == dst.height) {
        return false;
    }
    return true;
}

} // namespace InferenceBackend
