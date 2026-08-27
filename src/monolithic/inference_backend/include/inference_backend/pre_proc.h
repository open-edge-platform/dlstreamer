/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "image.h"
#include "input_image_layer_descriptor.h"

namespace InferenceBackend {

enum class ImagePreprocessorType : int {
    AUTO = 0,
    OPENCV,
    IE,
    VAAPI_SYSTEM,
    VAAPI_SURFACE_SHARING,
    D3D11,
    D3D11_SURFACE_SHARING,
    VAAPI_NPU_DMABUF
};
class ImagePreprocessor {
  public:
    static ImagePreprocessor *Create(ImagePreprocessorType type, const std::string custom_preproc_lib);

    virtual ~ImagePreprocessor() = default;

    virtual void Convert(const Image &src, Image &dst, const InputImageLayerDesc::Ptr &pre_proc_info = nullptr,
                         const ImageTransformationParams::Ptr &image_transform_info = nullptr, bool make_planar = true,
                         bool allocate_destination = false) = 0;
    virtual void ReleaseImage(const Image &dst) = 0; // to be called if Convert called with allocate_destination = true

  protected:
    bool needPreProcessing(const Image &src, Image &dst);
    bool needCustomImageConvert(const InputImageLayerDesc::Ptr &pre_proc_info);
};

Image ApplyCrop(const Image &src);

ImagePreprocessor *CreatePreProcOpenCV(const std::string custom_preproc_lib);
} // namespace InferenceBackend
