# ==============================================================================
# Copyright (C) 2021-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import sys
import gi
import numpy as np

gi.require_version('Gst', '1.0')
from gi.repository import GLib, Gst, GObject # pylint: disable=no-name-in-module

from gstgva import VideoFrame, Tensor, RegionOfInterest
Gst.init(sys.argv)


DETECTION_CONFEDENCE_THRESHOLD = 0.9
LABELS = ["person", "bicycle", "car", "motorbike", "aeroplane", "bus", "train", "truck", "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
          "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "sofa", "pottedplant", "bed", "diningtable", "toilet", "tvmonitor", "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"]
# n c h w
INPUT_SHAPE = (1, 3, 768, 1024)


class BBox:
    def __init__(self, top_left_x, top_left_y, bottom_right_x, bottom_right_y, confidence):
        self.top_left_x = top_left_x / INPUT_SHAPE[3]
        self.top_left_y = top_left_y / INPUT_SHAPE[2]
        self.bottom_right_x = bottom_right_x / INPUT_SHAPE[3]
        self.bottom_right_y = bottom_right_y / INPUT_SHAPE[2]
        self.confidence = confidence

    def __repr__(self) -> str:
        return f"{self.top_left_x}, {self.top_left_y}, {self.bottom_right_x}, {self.bottom_right_y}, {self.confidence}"


def process_labels(tensor):
    dims = tensor.dims()
    data = tensor.data()

    labels = [LABELS[int(id)] for id in data]

    return labels


def process_boxes(tensor):
    dims = tensor.dims()
    dims[0] = -1

    data = tensor.data()
    data = np.reshape(data, dims)

    bboxes = list()
    for box in data:
        bboxes.append(BBox(box[0], box[1], box[2], box[3], box[4]))

    return bboxes


def process_masks(tensor):
    dims = tensor.dims()
    dims[0] = -1

    return np.reshape(tensor.data(), dims)


def process_frame(frame: VideoFrame) -> bool:
    labels_out = None
    boxes_out = None
    masks_out = None

    for tensor in frame.tensors():
        layer_name = tensor.layer_name()
        if layer_name == 'labels':
            labels_out = tensor
        if layer_name == 'boxes':
            boxes_out = tensor
        if layer_name == 'masks':
            masks_out = tensor

    labels = list()
    if labels_out:
        labels = process_labels(labels_out)
    bboxes = list()
    if boxes_out:
        bboxes = process_boxes(boxes_out)
    masks = list()
    if masks_out:
        masks = process_masks(masks_out)

    for bbox, label, mask in zip(bboxes, labels, masks):
        if bbox.confidence > DETECTION_CONFEDENCE_THRESHOLD:
            frame.add_region(bbox.top_left_x, bbox.top_left_y, bbox.bottom_right_x - bbox.top_left_x,
                             bbox.bottom_right_y - bbox.top_left_y, label=label, confidence=bbox.confidence, normalized=True)

    return True
