/*******************************************************************************
 * Copyright (C) 2022-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "yolo_v5.h"

#include <cmath>

using namespace post_processing;

YOLOv3Converter::DetectedObject YOLOv5Converter::calculateBoundingBox(size_t col, size_t row, float raw_x, float raw_y,
                                                                      float raw_w, float raw_h, size_t side_w,
                                                                      size_t side_h, float input_width,
                                                                      float input_height, size_t mask_0,
                                                                      size_t bbox_cell_num, float confidence,
                                                                      float bbox_class_first) const {

    float x =
        static_cast<float>(col + 2 * (output_sigmoid_activation ? sigmoid(raw_x) : raw_x) - 0.5) / side_w * input_width;
    float y = static_cast<float>(row + 2 * (output_sigmoid_activation ? sigmoid(raw_y) : raw_y) - 0.5) / side_h *
              input_height;

    // TODO: check if index in array range
    const size_t anchor_offset = 2 * mask_0;
    float width = std::pow(sigmoid(raw_w) * 2, 2) * anchors[anchor_offset + 2 * bbox_cell_num];
    float height = std::pow(sigmoid(raw_h) * 2, 2) * anchors[anchor_offset + 2 * bbox_cell_num + 1];
    return DetectedObject(x, y, width, height, 0, confidence, bbox_class_first,
                          BlobToMetaConverter::getLabelByLabelId(bbox_class_first), 1.0f / input_width,
                          1.0f / input_height, true);
}