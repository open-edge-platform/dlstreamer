/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "yolo_v3.h"

namespace post_processing {

class YOLOv5Converter : public YOLOv3Converter {
  protected:
    YOLOv3Converter::DetectedObject calculateBoundingBox(size_t col, size_t row, float raw_x, float raw_y, float raw_w,
                                                         float raw_h, size_t side_w, size_t side_h, float input_width,
                                                         float input_height, size_t mask_0, size_t bbox_cell_num,
                                                         float confidence, float bbox_class_first) const override;

  public:
    YOLOv5Converter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold,
                    const YOLOBaseConverter::Initializer &yolo_initializer, const MaskType &masks)
        : YOLOv3Converter(std::move(initializer), confidence_threshold, iou_threshold, yolo_initializer, masks) {
    }
    static std::string getName() {
        return "yolo_v5";
    }
};
} // namespace post_processing
