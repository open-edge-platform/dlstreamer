/*******************************************************************************
 * Copyright (C) 2024-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_roi_converter.h"
#include "inference_backend/image_inference.h"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace post_processing {

/*
yolo_v7 tensor output = [B, N, 85] where:
    B - batch size
    N - number of detection boxes
Detection box has the [x, y, w, h, box_score, class_no_1, …, class_no_80] format, where:
    (x, y) - raw coordinates of box center
    w, h - raw width and height of box
    box_score - confidence of detection box
    class_no_1, …, class_no_80 - probability distribution over the classes
*/
const int YOLOV7_OFFSET_X = 0;  // x coordinate of bounding box center
const int YOLOV7_OFFSET_Y = 1;  // y coordinate of bounding box center
const int YOLOV7_OFFSET_W = 2;  // width of bounding box center
const int YOLOV7_OFFSET_H = 3;  // height of bounding box center
const int YOLOV7_OFFSET_BS = 4; // confidence of detection box
const int YOLOV7_OFFSET_CS = 5; // probability of class_0 ... class_n-1

class YOLOv7Converter : public BlobToROIConverter {
  protected:
    void parseOutputBlob(const float *data, const std::vector<size_t> &dims,
                         std::vector<DetectedObject> &objects) const;

  public:
    YOLOv7Converter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, true, iou_threshold) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "yolo_v7";
    }

    static std::string getDeprecatedName() {
        return "tensor_to_bbox_yolo_v7";
    }
};
} // namespace post_processing
