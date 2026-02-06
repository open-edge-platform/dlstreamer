/*******************************************************************************
 * Copyright (C) 2024-2026 Intel Corporation
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
#include <string>
#include <vector>

namespace post_processing {

/*
yolo_v8 object tensor output = [B, 84, N] where:
    B - batch size
    N - number of detection boxes
Detection box has the [x, y, w, h, [r], class_no_1, …, class_no_80, [mask_1, ..., mask_2]] format, where:
    (x, y) - raw coordinates of box center
    (w, h) - raw width and height of box
    class_no_1, …, class_no_80 - probability distribution over the classes
    [optional] r - radius of boudning box (object-oriented box models)
    [optional] nm - mask (instance segmentation models)
*/
const int YOLOV8_OFFSET_X = 0;  // x coordinate of bounding box center
const int YOLOV8_OFFSET_Y = 1;  // y coordinate of bounding box center
const int YOLOV8_OFFSET_W = 2;  // width of bounding box center
const int YOLOV8_OFFSET_H = 3;  // height of bounding box center
const int YOLOV8_OFFSET_CS = 4; // class probability

class YOLOv8Converter : public BlobToROIConverter {
  protected:
    void parseOutputBlob(const float *data, const std::vector<size_t> &dims, std::vector<DetectedObject> &objects,
                         bool oob) const;

  public:
    YOLOv8Converter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, true, iou_threshold) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "yolo_v8";
    }

    static std::string getDeprecatedName() {
        return "tensor_to_bbox_yolo_v8";
    }
};

class YOLOv8ObbConverter : public YOLOv8Converter {
  public:
    YOLOv8ObbConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : YOLOv8Converter(std::move(initializer), confidence_threshold, iou_threshold) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "yolo_v8_obb";
    }
};

/*
yolo_v8_pose object tensor output = [B, 56, N] where:
    B - batch size
    N - number of detection boxes
Detection box has the [x, y, w, h, confidence, keypoint_0_x, keypoint_0_y, keypoint_0_score, ..., ] format, where:
    (x, y) - raw coordinates of box center
    (w, h) - raw width and height of box
    confidence - box detection confidence
    keypoint (x, y, score) - keypoint coordinate within a box, keypoint detection confidence
*/

class YOLOv8PoseConverter : public YOLOv8Converter {
  protected:
    void parseOutputBlob(const float *data, const std::vector<size_t> &dims,
                         std::vector<DetectedObject> &objects) const;

  public:
    YOLOv8PoseConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : YOLOv8Converter(std::move(initializer), confidence_threshold, iou_threshold) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "yolo_v8_pose";
    }
};

class YOLOv8SegConverter : public YOLOv8Converter {
  protected:
    void parseOutputBlob(const float *boxes_data, const std::vector<size_t> &boxes_dims, const float *masks_data,
                         const std::vector<size_t> &masks_dims, std::vector<DetectedObject> &objects) const;

  public:
    YOLOv8SegConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : YOLOv8Converter(std::move(initializer), confidence_threshold, iou_threshold) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "yolo_v8_seg";
    }
};

} // namespace post_processing