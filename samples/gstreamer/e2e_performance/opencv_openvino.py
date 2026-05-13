# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""OpenCV + OpenVINO execution path.
Almost identical to run_object_detection() from the OpenVINO YOLO26 notebook:
https://github.com/openvinotoolkit/openvino_notebooks/blob/latest/notebooks/yolov26-optimization/yolov26-object-detection.ipynb
Lines changed are marked with comments.
"""

import collections
import time

import cv2
import numpy as np

from perf_comparison import SNAPSHOT_FRAMES, Result


def run(model_dir, video, num_frames, warmup):
    """Run object detection loop from the notebook."""
    from ultralytics import YOLO  # pylint: disable=import-outside-toplevel

    model = YOLO(str(model_dir), task="detect")
    # CHANGED: device set to GPU instead of notebook default CPU
    device = "gpu"

    cap = cv2.VideoCapture(str(video))

    # warmup
    for _ in range(warmup):
        ret, frame = cap.read()
        if not ret:
            break
        input_image = np.array(frame)
        model(input_image, verbose=False, device=f"intel:{device}")

    cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
    processing_times = collections.deque()
    count = 0

    while count < num_frames:
        # CHANGED: cv2.VideoCapture instead of notebook's VideoPlayer
        ret, frame = cap.read()
        if not ret:
            cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
            ret, frame = cap.read()

        # no explicit resize needed because model() handles letterbox/resize internally
        input_image = np.array(frame)

        start_time = time.time()
        model(input_image, verbose=False, device=f"intel:{device}")
        stop_time = time.time()

        processing_times.append(stop_time - start_time)
        if len(processing_times) > 200:
            processing_times.popleft()
        count += 1

    cap.release()

    processing_time = np.mean(processing_times) * 1000
    fps = 1000 / processing_time
    return Result(fps=fps, inference_ms=processing_time, frames=count)


def save_snapshot(model_dir, video, path):
    """Save first detection frame using ultralytics plot(), same as notebook."""
    from ultralytics import YOLO  # pylint: disable=import-outside-toplevel

    model = YOLO(str(model_dir), task="detect")
    # CHANGED: device set to GPU instead of notebook default CPU
    device = "gpu"

    cap = cv2.VideoCapture(str(video))
    for _ in range(SNAPSHOT_FRAMES):
        ret, frame = cap.read()
        if not ret:
            break
        input_image = np.array(frame)
        detections = model(input_image, verbose=False, device=f"intel:{device}")
        result = detections[0]
        if result.boxes is not None and len(result.boxes) > 0:
            annotated = result.plot()
            cv2.imwrite(str(path), annotated)
            break
    cap.release()
