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

// Camera calibration used by the MonoDETR ("mono3d") pipeline. P2 is a KITTI-style 3x4 projection
// matrix stored row-major. orig_width/orig_height are the pixel dimensions of the image the
// calibration refers to (and that the model's img_sizes input expects).
struct Mono3DCalibration {
    bool valid = false;
    std::array<double, 12> p2{};
    int orig_width = 0;
    int orig_height = 0;
};

// Parses a calibration file and derives P2:
//   - KITTI .txt: a line starting with "P2:" followed by 12 floats (row-major 3x4).
//   - JSON: "projection_matrix" (3x4, takes precedence) or "intrinsic_matrix" (3x3 -> P2 = [K|0]);
//           optional "image_size": [W, H].
// default_width/default_height fill the image size when the file doesn't specify one (e.g. KITTI).
// On any failure the returned calibration has valid == false (image-size defaults are still filled).
Mono3DCalibration parseMono3DCalibration(const std::string &calibration_file, int default_width, int default_height);

// Writes P2 (as a GST_TYPE_ARRAY of 12 doubles named "P2") and orig_width/orig_height (ints) onto
// the given structure. Used to feed both the model inputs (calib/img_sizes) and the mono3d converter.
void applyMono3DCalibrationToStructure(GstStructure *s, const Mono3DCalibration &calib);

} // namespace post_processing
