/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "frame_selector.hpp"

#include <algorithm>
#include <cmath>

namespace gvaframeselect {

std::vector<std::size_t> select_indices(std::size_t frame_count, std::size_t frame_budget) {
    if (frame_count == 0 || frame_budget == 0)
        return {};

    const std::size_t selected_count = std::min(frame_count, frame_budget);
    if (selected_count == 1)
        return {0};

    std::vector<std::size_t> indices;
    indices.reserve(selected_count);
    for (std::size_t i = 0; i < selected_count; ++i) {
        const long double position = static_cast<long double>(i) * static_cast<long double>(frame_count - 1) /
                                     static_cast<long double>(selected_count - 1);
        indices.push_back(static_cast<std::size_t>(std::round(position)));
    }
    return indices;
}

} // namespace gvaframeselect