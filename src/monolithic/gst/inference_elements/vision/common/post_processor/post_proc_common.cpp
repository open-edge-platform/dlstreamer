/*******************************************************************************
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "post_proc_common.h"

namespace post_processing {

void checkFramesAndTensorsTable(const FramesWrapper &frames, const TensorsTable &tensors) {
    if (frames.empty())
        throw std::invalid_argument("There are no inference frames");

    // The size of the metadata array is equal to batch size, and the number of frames can be less than it,
    // but not vice versa in case of total number of frames is not divisible by batch size.
    if (tensors.size() < frames.size())
        throw std::logic_error("The size of the metadata array is less than the size of the inference frames: " +
                               std::to_string(tensors.size()) + " / " + std::to_string(frames.size()));
}

} // namespace post_processing
