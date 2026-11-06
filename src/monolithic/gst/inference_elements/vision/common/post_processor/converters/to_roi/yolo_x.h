/*******************************************************************************
 * Copyright (C) 2024-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_roi_converter.h"
#include "inference_backend/image_inference.h"
#include <opencv2/opencv.hpp>

#include <gst/gst.h>

#include <map>
#include <memory>
#include <openvino/core/type/float16.hpp>
#include <string>
#include <vector>

namespace post_processing {

/*
yolo_x tensor output = [B, N, 85] where:
    B - batch size
    N - number of detection boxes
Detection box has the [x, y, w, h, box_score, class_no_1, …, class_no_80] format, where:
    (x, y) - raw coordinates of box center
    w, h - raw width and height of box
    box_score - confidence of detection box
    class_no_1, …, class_no_80 - probability distribution over the classes
*/
const int OFFSET_X = 0;  // x coordinate of bounding box center
const int OFFSET_Y = 1;  // y coordinate of bounding box center
const int OFFSET_W = 2;  // width of bounding box center
const int OFFSET_H = 3;  // height of bounding box center
const int OFFSET_BS = 4; // confidence of detection box
const int OFFSET_CS = 5; // probability of class_0 ... class_n-1

class YOLOxConverter : public BlobToROIConverter {
  protected:
    void parseOutputBlob(const float *data, const std::vector<size_t> &dims,
                         std::vector<DetectedObject> &objects) const;

    const int NUM_CLASSES;

  public:
    YOLOxConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold,
                   int classes)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, true, iou_threshold), NUM_CLASSES(classes) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "yolo_x";
    }
};
} // namespace post_processing