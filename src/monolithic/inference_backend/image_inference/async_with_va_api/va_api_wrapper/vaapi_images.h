/*******************************************************************************
 * Copyright (C) 2019-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "vaapi_context.h"
#include "vaapi_image_map.h"
#include "vaapi_utils.h"

#include "inference_backend/image.h"

#include <algorithm>
#include <future>
#include <memory>
#include <vector>

namespace InferenceBackend {

struct VaApiImage {
    VaApiContext *context = nullptr;
    Image image = Image();
    std::future<void> sync;
    bool completed = true;
    std::unique_ptr<ImageMap> image_map;
    uint32_t scaling_flags = VA_FILTER_SCALING_DEFAULT;

    VaApiImage();
    VaApiImage(VaApiContext *context_, uint32_t width, uint32_t height, int format, MemoryType memory_type,
               uint32_t scaling_flgs = VA_FILTER_SCALING_DEFAULT);
    ~VaApiImage();

    Image Map();
    void Unmap();

    VaApiImage(const VaApiImage &) = delete;
    VaApiImage &operator=(const VaApiImage &) = delete;
};

class VaApiImagePool {
    std::vector<std::unique_ptr<VaApiImage>> _images;
    std::condition_variable _free_image_condition_variable;
    std::mutex _free_images_mutex;

  public:
    VaApiImage *AcquireBuffer();
    void ReleaseBuffer(VaApiImage *image);
    struct ImageInfo {
        uint32_t width;
        uint32_t height;
        uint32_t batch;
        FourCC format;
        MemoryType memory_type;
    };

    struct SizeParams {
        // Number of items in the pool with default scaling method
        uint32_t num_default = 0;
        // Number of items in the pool with fast scaling method
        uint32_t num_fast = 0;

        SizeParams(uint32_t num_default_scale, uint32_t num_fast_scale) noexcept
            : num_default(num_default_scale), num_fast(num_fast_scale) {
        }
        explicit SizeParams(uint32_t pool_size) noexcept : SizeParams(pool_size, 0) {
        }
        explicit SizeParams() = default;

        // Returns total size (items) of the pool
        size_t size() const {
            return num_default + num_fast;
        }
    };

    VaApiImagePool(VaApiContext *context, SizeParams size_params, ImageInfo info);

    void Flush();
};

} // namespace InferenceBackend
