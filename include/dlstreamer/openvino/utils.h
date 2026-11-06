/*******************************************************************************
 * Copyright (C) 2022-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "dlstreamer/frame.h"
#include <openvino/openvino.hpp>

#include <cstdint>
#include <cstring>
#include <string>

namespace dlstreamer {

static inline DataType data_type_from_openvino(ov::element::Type element) {
    switch (element) {
    case ov::element::Type_t::u8:
        return DataType::UInt8;
    case ov::element::Type_t::f32:
        return DataType::Float32;
    case ov::element::Type_t::i32:
        return DataType::Int32;
    case ov::element::Type_t::i64:
        return DataType::Int64;
    default:
        break;
    }
    throw std::runtime_error("Unsupported OV element type: " + element.get_type_name());
}

static inline ov::element::Type data_type_to_openvino(DataType type) {
    switch (type) {
    case DataType::UInt8:
        return ov::element::Type_t::u8;
    case DataType::Float32:
        return ov::element::Type_t::f32;
    case DataType::Int32:
        return ov::element::Type_t::i32;
    case DataType::Int64:
        return ov::element::Type_t::i64;
    default:
        throw std::runtime_error("Unsupported DataType");
    }
}

static inline TensorInfo ov_tensor_to_tensor_info(ov::Tensor tensor) {
    return TensorInfo(tensor.get_shape(), data_type_from_openvino(tensor.get_element_type()), tensor.get_strides());
}

// Adapter LUID of an OpenVINO GPU, packed like the "adapter-luid" of GStreamer's D3D11/D3D12
// devices (LowPart in the low 4 bytes). Throws if the device doesn't exist or has no LUID.
static inline int64_t gpu_adapter_luid(ov::Core &core, const std::string &device) {
    const auto luid = core.get_property(device, ov::device::luid);
    int64_t packed = 0;
    std::memcpy(&packed, luid.luid.data(), sizeof(packed));
    return packed;
}

// The OpenVINO GPU ("GPU" or "GPU.<N>") on the adapter with this LUID, empty if there is none.
// DXGI's adapter order doesn't match OpenVINO's GPU order, so only the LUID identifies it.
static inline std::string find_gpu_for_adapter_luid(ov::Core &core, int64_t luid) {
    for (const auto &device : core.get_available_devices()) {
        if (device.rfind("GPU", 0) != 0)
            continue;
        try {
            if (gpu_adapter_luid(core, device) == luid)
                return device;
        } catch (...) {
            // No LUID for this device: it can't be the one
        }
    }
    return {};
}

} // namespace dlstreamer
