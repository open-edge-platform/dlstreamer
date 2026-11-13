/*******************************************************************************
 * Copyright (C) 2024-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "yolo_v4.h"

#include <opencv2/core.hpp>

using namespace post_processing;

void YOLOv4Converter::parseOutputBlob(const float *blob_data, const std::vector<size_t> &blob_dims, size_t blob_size,
                                      std::vector<DetectedObject> &objects) const {

    // Transpose output data from NHWC to NCHW layout

    if (!blob_data)
        throw std::invalid_argument("Output blob data is nullptr.");

    auto desc = LayoutDesc::fromLayout(output_dims_layout);
    if (!desc)
        throw std::runtime_error("Unsupported output layout.");

    size_t _N = blob_dims[desc.N];
    size_t _C = blob_dims[desc.B];
    size_t _W = blob_dims[desc.Cx];
    size_t _H = blob_dims[desc.Cy];

    std::vector<float> _transposed_blob_data(blob_size, 0.0f);

    size_t offset_nhwc = 0; // n * HWC + h * WC + w * C + c
    size_t offset_nchw = 0; // n * CHW + c * HW + h * W + w
    for (size_t iN = 0; iN < _N; iN++) {
        for (size_t iH = 0; iH < _H; iH++) {
            for (size_t iW = 0; iW < _W; iW++) {
                for (size_t iC = 0; iC < _C; iC++) {
                    offset_nhwc = iN * _H * _W * _C + iH * _W * _C + iW * _C + iC;
                    offset_nchw = iN * _C * _H * _W + iC * _H * _W + iH * _W + iW;
                    // NHWC -> NCHW
                    _transposed_blob_data[offset_nchw] = blob_data[offset_nhwc];
                }
            }
        }
    }

    YOLOv3Converter::parseOutputBlob(_transposed_blob_data.data(), blob_dims, blob_size, objects);
}