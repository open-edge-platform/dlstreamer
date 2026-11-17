/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// The shared ov::Core of the OpenVINO backend, and queries on it. Include it wherever a model is read, compiled or
// queried.

#pragma once

#include <openvino/runtime/core.hpp>

#include <string>

namespace InferenceBackend {
namespace OpenVino {

// One ov::Core per process, shared by every inference instance.
inline ov::Core &OpenVinoCore() {
    static ov::Core ovcore;
    return ovcore;
}

// Batch size the backend uses when batch-size is 0 (auto): ov::optimal_batch_size, or 1 if the device has none.
inline int GetDeviceOptimalBatchSize(const std::string &device) {
    try {
        return static_cast<int>(OpenVinoCore().get_property(device, ov::optimal_batch_size));
    } catch (...) {
        return 1;
    }
}

} // namespace OpenVino
} // namespace InferenceBackend
