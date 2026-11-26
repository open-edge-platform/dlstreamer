/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/base/frame.h"

#ifdef __linux__
#include <unistd.h>
#endif

namespace dlstreamer {

namespace tensor::key {
static constexpr auto dma_fd = "dma_fd";             // (int)
static constexpr auto drm_modifier = "drm_modifier"; // (uint64_t) DRM format modifier, e.g. tiling
}; // namespace tensor::key

class DMATensor : public BaseTensor {
  public:
    DMATensor(int dma_fd, uint64_t drm_modifier, const TensorInfo &info, bool take_ownership = false,
              ContextPtr context = nullptr)
        : BaseTensor(MemoryType::DMA, info, tensor::key::dma_fd, context), _take_ownership(take_ownership) {
#ifndef __linux__
        throw std::runtime_error("DMABuffer is not supported");
#endif
        set_handle(tensor::key::dma_fd, dma_fd);
        set_handle(tensor::key::drm_modifier, static_cast<handle_t>(drm_modifier));
    }

    int dma_fd() const {
        return handle(tensor::key::dma_fd);
    }

    uint64_t drm_modifier() const {
        return static_cast<uint64_t>(handle(tensor::key::drm_modifier));
    }

    size_t offset() const {
        return handle(tensor::key::offset, 0);
    }

    ~DMATensor() {
#ifdef __linux__
        if (_take_ownership) {
            try {
                int fd = handle(tensor::key::dma_fd);
                close(fd);
            } catch (const std::runtime_error &e) {
                // GST_ERROR("Failed to close DMA handle: %s", e.what());
            } catch (...) {
                // GST_ERROR("Unknown exception occurred while closing DMA handle");
            }
        }
#endif
    }

  private:
    bool _take_ownership = false;
};

using DMATensorPtr = std::shared_ptr<DMATensor>;

} // namespace dlstreamer
