/*******************************************************************************
 * Copyright (C) 2018-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <inference_backend/image_inference.h>

#include <gst/gst.h>
#include <string>
#include <unordered_set>

struct Memory;

class GstAllocatorWrapper : public InferenceBackend::Allocator {
  private:
    std::string name;
    std::shared_ptr<GstAllocator> allocator;

  public:
    GstAllocatorWrapper(const std::string &name);
    ~GstAllocatorWrapper() = default;

    // InferenceBackend::Allocator
    void Alloc(size_t size, void *&buffer_ptr, AllocContext *&alloc_context);
    void Free(AllocContext *alloc_context);
};
