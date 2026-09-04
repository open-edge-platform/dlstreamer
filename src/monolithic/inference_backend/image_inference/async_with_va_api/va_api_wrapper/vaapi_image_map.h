/*******************************************************************************
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "vaapi_utils.h"

#include "inference_backend/image.h"

#include <functional>

namespace InferenceBackend {

class VaApiImageMap_SystemMemory : public ImageMap {
  public:
    VaApiImageMap_SystemMemory();
    ~VaApiImageMap_SystemMemory();

    Image Map(const Image &image) override;
    void Unmap() override;

  protected:
    VADisplay va_display;
    VAImage va_image;
};

class VaApiImageMap_VASurface : public ImageMap {
  public:
    VaApiImageMap_VASurface();
    ~VaApiImageMap_VASurface();

    Image Map(const Image &image) override;
    void Unmap() override;
};

// Zero-copy: DMA-BUF backed surface, no GPU→CPU readback needed.
// The Map() returns an Image whose dma_fd and va_surface_id point to the same physical buffer.
class VaApiImageMap_DmaBuf : public ImageMap {
  public:
    VaApiImageMap_DmaBuf();
    ~VaApiImageMap_DmaBuf();

    Image Map(const Image &image) override;
    void Unmap() override;
};

} // namespace InferenceBackend
