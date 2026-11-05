/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// The shared ov::Core of the OpenVINO backend. Include it wherever a model is read, compiled or queried.

#pragma once

#include <openvino/runtime/core.hpp>

namespace InferenceBackend {
namespace OpenVino {

// One ov::Core per process, shared by every inference instance.
inline ov::Core &openvino_core() {
    static ov::Core ovcore;
    return ovcore;
}

} // namespace OpenVino
} // namespace InferenceBackend
