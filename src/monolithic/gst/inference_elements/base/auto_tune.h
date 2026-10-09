/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <string>

namespace dlstreamer {
namespace autotune {

// Tier 0 ("static") pipeline auto-tuning: deterministic, measurement-free
// resolution of the categorical performance choices. Currently this resolves
// the best inference device; the existing pre-process-backend auto-selection
// then derives the matching zero-copy memory path from that device.

// Returns true when static auto-tuning is opted in via the GST_DLS_AUTOTUNE
// environment variable (values: "static"/"on"/"1"/"true"; anything else, or
// unset, is treated as disabled).
bool static_tuning_enabled();

// Enumerates the available OpenVINO devices and returns the best one for
// inference throughput ("GPU", "GPU.1", "NPU", "CPU", ...). Discrete GPUs are
// preferred over integrated, then NPU, then CPU. Returns an empty string if
// device introspection is unavailable, in which case the caller should keep the
// current device unchanged. The result is cached after the first successful call.
std::string select_best_device();

} // namespace autotune
} // namespace dlstreamer
