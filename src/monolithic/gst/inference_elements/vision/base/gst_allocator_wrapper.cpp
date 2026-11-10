/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gst_allocator_wrapper.h"
#include <inference_backend/logger.h>
#include <sstream>

using namespace std;
using namespace InferenceBackend;

namespace {

template <typename T>
void stream_string_builder(stringstream &stream, T t) {
    stream << t;
}

template <typename T, typename... Args>
void stream_string_builder(stringstream &stream, T t, Args... args) {
    stream << t;
    stream_string_builder(stream, args...);
}

template <typename... Args>
string build_string(Args... args) {
    stringstream stream;
    stream_string_builder(stream, args...);
    return stream.str();
}

shared_ptr<GstAllocator> create_gst_allocator(const std::string &name) {
    const char *allocator_name = nullptr;
    if (!name.empty() && name != "default") {
        allocator_name = name.c_str();
        GVA_TRACE("The '%s' will be used as allocator name", allocator_name);
    } else {
        GVA_WARNING("Allocator name is empty. Default gstreamer allocator will be used");
    }

    auto allocator = shared_ptr<GstAllocator>(gst_allocator_find(allocator_name), gst_object_unref);

    if (allocator == nullptr && !name.empty()) {
        GVA_WARNING("Cannot find allocator '%s'. Fallback to default gstreamer allocator", name.c_str());
    } else {
        GVA_TRACE("Allocator is initialized");
    }

    return allocator;
}

} // namespace

struct Memory {
    shared_ptr<GstAllocator> allocator;
    GstMapInfo map_info;
    shared_ptr<GstMemory> memory;

    static std::shared_ptr<Memory> create(size_t size, shared_ptr<GstAllocator> allocator = nullptr) {
        std::shared_ptr<Memory> memory = nullptr;
        try {
            memory = std::make_shared<Memory>(size, allocator);
        } catch (const exception &e) {
            GVA_ERROR("An error occured while creating Memory object: %s", e.what());
        }
        return memory;
    }

    Memory(const Memory &) = delete;

    Memory(size_t size, shared_ptr<GstAllocator> allocator = nullptr) : allocator(allocator) {
        const auto deleter = [allocator](GstMemory *memory) { gst_allocator_free(allocator.get(), memory); };
        memory = shared_ptr<GstMemory>(gst_allocator_alloc(allocator.get(), size, nullptr), deleter);
        if (memory == nullptr) {
            throw runtime_error("Could not allocate memory");
        }
        if (gst_memory_map(memory.get(), &map_info, GST_MAP_WRITE) != TRUE) {
            throw runtime_error("Could not map memory");
        }
    }

    ~Memory() {
        gst_memory_unmap(memory.get(), &map_info);
    }
};

GstAllocatorWrapper::GstAllocatorWrapper(const std::string &name) : name(name) {
    if (name.empty()) {
        throw std::runtime_error("Cannot initialize wrapper: allocator's name is empty");
    }
    allocator = create_gst_allocator(name);
}

void GstAllocatorWrapper::Alloc(size_t size, void *&buffer_ptr, AllocContext *&alloc_context) {
    GVA_TRACE("Memory allocation initiated");

    auto memory = Memory::create(size, allocator);

    if (memory == nullptr) {
        string str = build_string("Could not allocate given size of memory (", size, ")");
        GVA_ERROR("%s", str.c_str());
        throw runtime_error(str.c_str());
    }

    alloc_context = reinterpret_cast<AllocContext *>(memory.get());
    buffer_ptr = reinterpret_cast<void *>(memory->map_info.data);

    GVA_TRACE("Memory allocated");
}

void GstAllocatorWrapper::Free(AllocContext *alloc_context) {
    GVA_TRACE("Memory deallocation initiated");

    if (alloc_context == nullptr) {
        GVA_ERROR("nullptr context is passed to deallocate");
        return;
    }
    try {
        Memory *memory = reinterpret_cast<Memory *>(alloc_context);
        delete memory;
        GVA_TRACE("Memory deallocated");
    } catch (const exception &e) {
        // TODO: log
        GVA_ERROR("Memory deallocation failed: %s", e.what());
        throw;
    }
    GVA_TRACE("Memory deallocated");
}
