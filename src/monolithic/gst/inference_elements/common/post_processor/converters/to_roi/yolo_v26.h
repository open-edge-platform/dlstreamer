/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "yolo_v10.h"

namespace post_processing {

// yolo_v26 uses same tensor output layout as yolo_v10 for detection tasks

class YOLOv26Converter : public YOLOv10Converter {
  public:
    YOLOv26Converter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : YOLOv10Converter(std::move(initializer), confidence_threshold, iou_threshold) {
        // YOLOv26, like YOLOv10, does not require NMS, this is a non-max suppression free model
    }

    static std::string getName() {
        return "yolo_v26";
    }
};

/*
yolo_v26 tensor output for tasks not supported in yolo_v10 = [B, N, 6+] where:
    B - batch size
    N - number of detection boxes (=300)
Detection box has the [x, y, w, h, box_score, labels] format, where:
    (x1, y1) - raw coordinates of the upper left corner of the bounding box
    (x2, y2) - raw coordinates of the bottom right corner of the bounding box
    box_score - confidence of detection box
    labels - label of detected object
    [for OBB task]
        angle - rotation angle of the bounding box
    [for pose task]
        keypoint (x, y, score) - keypoint coordinate within a box, keypoint detection confidence
    [for seg task]
        mask scores - mask coefficients
*/
const int YOLOV26_OFFSET_X1 = 0; // x coordinate of the upper left corner of the bounding box
const int YOLOV26_OFFSET_Y1 = 1; // y coordinate of the upper left corner of the bounding box
const int YOLOV26_OFFSET_X2 = 2; // x coordinate of the bottom right corner of the bounding box
const int YOLOV26_OFFSET_Y2 = 3; // y coordinate of the bottom right corner of the bounding box
const int YOLOV26_OFFSET_BS = 4; // confidence of detection box
const int YOLOV26_OFFSET_L = 5;  // labels

class YOLOv26ObbConverter : public YOLOv26Converter {
  public:
    YOLOv26ObbConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : YOLOv26Converter(std::move(initializer), confidence_threshold, iou_threshold) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "yolo_v26_obb";
    }
};

class YOLOv26PoseConverter : public YOLOv26Converter {
  protected:
    void parseOutputBlob(const float *data, const std::vector<size_t> &dims,
                         std::vector<DetectedObject> &objects) const;

  public:
    YOLOv26PoseConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold,
                         double iou_threshold)
        : YOLOv26Converter(std::move(initializer), confidence_threshold, iou_threshold) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "yolo_v26_pose";
    }
};

class YOLOv26SegConverter : public YOLOv26Converter {
  protected:
    void parseOutputBlob(const float *boxes_data, const std::vector<size_t> &boxes_dims, const float *masks_data,
                         const std::vector<size_t> &masks_dims, std::vector<DetectedObject> &objects) const;

  public:
    YOLOv26SegConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : YOLOv26Converter(std::move(initializer), confidence_threshold, iou_threshold) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "yolo_v26_seg";
    }
};

} // namespace post_processing