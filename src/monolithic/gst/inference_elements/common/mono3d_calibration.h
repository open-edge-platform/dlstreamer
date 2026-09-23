/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>

#include <array>
#include <string>

namespace post_processing {

// Camera calibration used by the MonoDETR ("mono3d") pipeline. P2 is a KITTI-style
// 3x4 projection matrix stored row-major. orig_width/orig_height are the pixel
// dimensions of the image the calibration refers to (and that the model's img_sizes
// input expects).
struct Mono3DCalibration {
    bool valid = false;
    std::array<double, 12> p2{};
    int orig_width = 0;
    int orig_height = 0;
};

// Parses a JSON intrinsics file (same schema as gvawatermark3d) and derives P2:
//   - "projection_matrix": 3x4 array  -> used directly as P2 (takes precedence)
//   - "intrinsic_matrix":  3x3 array  -> P2 = [K | 0]
//   - "image_size": [W, H]            -> original image size (optional)
// default_width/default_height are used for the image size when "image_size" is absent.
// On any failure the returned calibration has valid == false (but image size defaults are filled).
Mono3DCalibration parseMono3DIntrinsics(const std::string &intrinsics_file, int default_width, int default_height);

// Writes P2 (as a GST_TYPE_ARRAY of 12 doubles named "P2") and orig_width/orig_height
// (as ints) onto the given structure. Used to feed both the model inputs (calib/img_sizes)
// and the mono3d post-processor.
void applyMono3DCalibrationToStructure(GstStructure *s, const Mono3DCalibration &calib);

} // namespace post_processing
