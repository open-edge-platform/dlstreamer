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
Three tensors output:
mask_rcnn boxes output = [B, N, 5] where:
    B - batch size
    N - number of detection boxes
Detection box has the [x1, y1, x2, y2, box_score] format, where:
    (x1, y1) - raw coordinates of the upper left corner of the bounding box
    (x2, y2) - raw coordinates of the bottom right corner of the bounding box
    box_score - confidence of detection box
-------------------------------------------------------------------------------
mask_rcnn labels output = [B, N] where:
    B - batch size
    N - number of detection boxes
Each of the N neurons in the batch matches a box with the same index
and contains the index of the class assigned to that box.
-------------------------------------------------------------------------------
mask_rcnn masks output = [B, N, 28, 28] where:
    B - batch size
    N - number of detection boxes
*/
const int THREE_TENSORS_OFFSET_X1 = 0; // x coordinate of the upper left corner of the bounding box
const int THREE_TENSORS_OFFSET_Y1 = 1; // y coordinate of the upper left corner of the bounding box
const int THREE_TENSORS_OFFSET_X2 = 2; // x coordinate of the bottom right corner of the bounding box
const int THREE_TENSORS_OFFSET_Y2 = 3; // y coordinate of the bottom right corner of the bounding box
const int THREE_TENSORS_OFFSET_BS = 4; // confidence of detection box

const std::string THREE_TENSORS_BOXES_KEY = "boxes";
const std::string THREE_TENSORS_LABELS_KEY = "labels";
const std::string THREE_TENSORS_MASKS_KEY = "masks";

/*
Two tensors output:
mask_rcnn boxes output = [N, 7] where:
    N - number of detection boxes
Detection box has the [image_id, label, conf, x_1, y_1, x_2, y_2] format, where:
    image_id - ID of the image in the batch
    label - predicted class ID
    conf - confidence for the predicted class
    (x_1, y_1) - coordinates of the top left bounding box corner
    (x_2, y_2) - coordinates of the bottom right bounding box corner
    *coordinates stored in normalized format, in range [0, 1]*
-------------------------------------------------------------------------------
mask_rcnn masks output = [N, 90, 33, 33] where:
    N - number of detection boxes
    90 - the number of classes
*/
const int TWO_TENSORS_OFFSET_ID = 0; // ID of the image in the batch
const int TWO_TENSORS_OFFSET_CS = 1; // predicted class ID
const int TWO_TENSORS_OFFSET_BS = 2; // confidence of detection box
const int TWO_TENSORS_OFFSET_X1 = 3; // x coordinate of the upper left corner of the bounding box
const int TWO_TENSORS_OFFSET_Y1 = 4; // y coordinate of the upper left corner of the bounding box
const int TWO_TENSORS_OFFSET_X2 = 5; // x coordinate of the bottom right corner of the bounding box
const int TWO_TENSORS_OFFSET_Y2 = 6; // y coordinate of the bottom right corner of the bounding box

const std::string TWO_TENSORS_BOXES_KEY = "reshape_do_2d";
const std::string TWO_TENSORS_MASKS_KEY = "SecondStageBoxPredictor_1/Conv_3/BiasAdd";

class MaskRCNNConverter : public BlobToROIConverter {
  public:
    MaskRCNNConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold, double iou_threshold)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, true, iou_threshold) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "mask_rcnn";
    }

    static std::string getDeprecatedName() {
        return "tensor_to_bbox_mask_rcnn";
    }
};
} // namespace post_processing
