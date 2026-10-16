/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <cstddef>
#include <vector>

namespace gvaframeselect {

std::vector<std::size_t> select_indices(std::size_t frame_count, std::size_t frame_budget);

} // namespace gvaframeselect