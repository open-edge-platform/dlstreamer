/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "auto_tune.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <openvino/openvino.hpp>

namespace dlstreamer {
namespace autotune {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Family base scores; discrete GPUs get an additional bonus so dGPU > iGPU.
int base_family_score(const std::string &device) {
    if (device.rfind("GPU", 0) == 0)
        return 100;
    if (device.rfind("NPU", 0) == 0)
        return 50;
    if (device.rfind("CPU", 0) == 0)
        return 10;
    return 1;
}

int score_device(ov::Core &core, const std::string &device) {
    int score = base_family_score(device);
    try {
        auto type = core.get_property(device, ov::device::type);
        if (type == ov::device::Type::DISCRETE)
            score += 100;
    } catch (...) {
        // DEVICE_TYPE not reported; keep the family base score.
    }
    return score;
}

} // namespace

bool static_tuning_enabled() {
    const char *env = std::getenv("GST_DLS_AUTOTUNE");
    if (!env)
        return false;
    const std::string value = to_lower(env);
    return value == "static" || value == "on" || value == "1" || value == "true";
}

std::string select_best_device() {
    static std::once_flag once;
    static std::string cached;

    std::call_once(once, [] {
        try {
            ov::Core core;
            const std::vector<std::string> devices = core.get_available_devices();

            int best_score = -1;
            for (const auto &device : devices) {
                const int score = score_device(core, device);
                if (score > best_score) {
                    best_score = score;
                    cached = device;
                }
            }
        } catch (...) {
            // OpenVINO or the driver stack is unavailable: leave cached empty so
            // the caller keeps the user/default device untouched.
            cached.clear();
        }
    });

    return cached;
}

} // namespace autotune
} // namespace dlstreamer
