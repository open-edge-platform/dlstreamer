/*******************************************************************************
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "yolo_v3.h"

namespace post_processing {

class YOLOv4Converter : public YOLOv3Converter {
  protected:
    void parseOutputBlob(const float *blob_data, const std::vector<size_t> &blob_dims, size_t blob_size,
                         std::vector<DetectedObject> &objects) const override;

  public:
    YOLOv4Converter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold,
                    const YOLOBaseConverter::Initializer &yolo_initializer, const MaskType &masks)
        : YOLOv3Converter(std::move(initializer), confidence_threshold, iou_threshold, yolo_initializer, masks) {
    }
    static std::string getName() {
        return "yolo_v4";
    }
};
} // namespace post_processing
