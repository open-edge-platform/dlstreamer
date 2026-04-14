/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <memory>
#include <stdexcept>

namespace dlstreamer {

/**
 * @brief Enum lists supported memory types. Memory type CPU may work with any CPU-accessible memory buffers, other
 * memory types assume memory allocation on CPU or GPU via underlying framework and data access via framework-specific
 * memory handles, for example cl_mem in OpenCL.
 */
enum class MemoryType {
    Any = 0,

    // Direct pointers
    CPU = 0x1,
    USM = 0x2,

    // Memory handles
    DMA = 0x10,
    OpenCL = 0x20,
    VAAPI = 0x40,
    GST = 0x80,
    OpenCV = 0x200,
    OpenCVUMat = 0x400,
    OpenVINO = 0x8000,
    PyTorch = 0x10000,
    TensorFlow = 0x20000,
    VA = 0x40000,
    D3D11 = 0x80000
};

template <typename T_DOWN, typename T_UP>
static inline std::shared_ptr<T_DOWN> ptr_cast(const std::shared_ptr<T_UP> &ptr_up) {
    auto ptr_down = std::dynamic_pointer_cast<T_DOWN>(ptr_up);
    if (!ptr_down)
        throw std::runtime_error("Error casting to " + std::string(typeid(T_DOWN).name()));
    return ptr_down;
}

inline const char *memory_type_to_string(MemoryType type) {
    switch (type) {
    case MemoryType::CPU:
        return "System";
    case MemoryType::GST:
        return "GStreamer";
    case MemoryType::VAAPI:
        return "VASurface";
    case MemoryType::DMA:
        return "DMABuf";
    case MemoryType::USM:
        return "USM";
    case MemoryType::OpenCL:
        return "OpenCL";
    case MemoryType::OpenCV:
        return "OpenCV";
    case MemoryType::OpenCVUMat:
        return "OpenCVUMat";
    case MemoryType::OpenVINO:
        return "OpenVINO";
    case MemoryType::PyTorch:
        return "PyTorch";
    case MemoryType::TensorFlow:
        return "TensorFlow";
    case MemoryType::VA:
        return "VAMemory";
    case MemoryType::D3D11:
        return "D3D11Memory";
    case MemoryType::Any:
        return "Any";
    }
    throw std::runtime_error("Unknown MemoryType");
}

static inline MemoryType memory_type_from_string(std::string str) {
    if (str == "System" || str == "SystemMemory")
        return MemoryType::CPU;
    if (str == "GStreamer")
        return MemoryType::GST;
    if (str == "VASurface")
        return MemoryType::VAAPI;
    if (str == "DMABuf")
        return MemoryType::DMA;
    if (str == "USM")
        return MemoryType::USM;
    if (str == "OpenCL")
        return MemoryType::OpenCL;
    if (str == "OpenVINO")
        return MemoryType::OpenVINO;
    if (str == "TensorFlow")
        return MemoryType::TensorFlow;
    if (str == "Any")
        return MemoryType::Any;
    throw std::runtime_error("Unknown MemoryType string");
}

} // namespace dlstreamer
