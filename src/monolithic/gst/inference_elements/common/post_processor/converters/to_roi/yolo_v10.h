/*******************************************************************************
 * Copyright (C) 2024-2026 Intel Corporation
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
yolo_v10 tensor output = [B, N, 6] where:
    B - batch size
    N - number of detection boxes (=300)
Detection box has the [x, y, w, h, box_score, labels] format, where:
    (x1, y1) - raw coordinates of the upper left corner of the bounding box
    (x2, y2) - raw coordinates of the bottom right corner of the bounding box
    box_score - confidence of detection box
    labels - label of detected object
*/
const int YOLOV10_OFFSET_X1 = 0; // x coordinate of the upper left corner of the bounding box
const int YOLOV10_OFFSET_Y1 = 1; // y coordinate of the upper left corner of the bounding box
const int YOLOV10_OFFSET_X2 = 2; // x coordinate of the bottom right corner of the bounding box
const int YOLOV10_OFFSET_Y2 = 3; // y coordinate of the bottom right corner of the bounding box
const int YOLOV10_OFFSET_BS = 4; // confidence of detection box
const int YOLOV10_OFFSET_L = 5;  // labels

class YOLOv10Converter : public BlobToROIConverter {
  protected:
    void parseOutputBlob(const float *data, const std::vector<size_t> &dims, std::vector<DetectedObject> &objects,
                         bool oob) const;

  public:
    YOLOv10Converter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, false, iou_threshold) {
        // YOLOv10 does not require NMS, this is a non-max suppression free model, so set need_nms to false
    }

    const float *parseOutputBlob(const OutputBlobs &output_blobs, const std::string &key, size_t batch_size,
                                 size_t batch_number) const;
    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "yolo_v10";
    }
};

} // namespace post_processing