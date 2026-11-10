/*******************************************************************************
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "yolo_v8.h"

// yolo_v11 uses same tensor output layout as yolo_v8

namespace post_processing {

class YOLOv11Converter : public YOLOv8Converter {
  public:
    YOLOv11Converter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : YOLOv8Converter(std::move(initializer), confidence_threshold, iou_threshold) {
    }

    static std::string getName() {
        return "yolo_v11";
    }
};

class YOLOv11ObbConverter : public YOLOv8ObbConverter {
  public:
    YOLOv11ObbConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : YOLOv8ObbConverter(std::move(initializer), confidence_threshold, iou_threshold) {
    }

    static std::string getName() {
        return "yolo_v11_obb";
    }
};

class YOLOv11PoseConverter : public YOLOv8PoseConverter {
  public:
    YOLOv11PoseConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold,
                         double iou_threshold)
        : YOLOv8PoseConverter(std::move(initializer), confidence_threshold, iou_threshold) {
    }

    static std::string getName() {
        return "yolo_v11_pose";
    }
};

class YOLOv11SegConverter : public YOLOv8SegConverter {
  public:
    YOLOv11SegConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : YOLOv8SegConverter(std::move(initializer), confidence_threshold, iou_threshold) {
    }

    static std::string getName() {
        return "yolo_v11_seg";
    }
};

} // namespace post_processing