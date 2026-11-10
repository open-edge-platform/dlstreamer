/*******************************************************************************
 * Copyright (C) 2020-2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
#pragma once

#include <gst/gst.h>

#include <functional>
#include <memory>
#include <stdexcept>

using GValueArrayUniquePtr = std::unique_ptr<GValueArray, std::function<void(GValueArray *)>>;
using GstStructureUniquePtr = std::unique_ptr<GstStructure, std::function<void(GstStructure *)>>;
using GstStructureSharedPtr = std::shared_ptr<GstStructure>;

template <typename Type, typename CopyFunctionType>
Type *copy(const Type *pointer, CopyFunctionType copy_function) {
    if (copy_function == nullptr)
        throw std::invalid_argument("Copy function is empty");
    Type *copied = nullptr;
    if (pointer != nullptr) {
        copied = copy_function(pointer);
        if (copied == nullptr)
            throw std::runtime_error("Could not copy memory");
    }
    return copied;
}
