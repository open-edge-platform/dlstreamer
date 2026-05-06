# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""OpenCV + OpenVINO execution path.

Synchronous decode-preprocess-infer loop comparable to the OpenVINO YOLO26
notebook approach, but targeting iGPU for fair comparison with DLStreamer.
"""

import time

import cv2
import numpy as np
import openvino as ov

from perf_comparison import (CONFIDENCE_THRESHOLD, INFERENCE_DEVICE,
                              LETTERBOX_PAD_VALUE, SNAPSHOT_FRAMES,
                              YOLO_INPUT_SIZE, COLORS, PipelineError,
                              compute_result, Result)


def _read_frame(cap):
    """Read next frame, loop if video ends."""
    ok, frame = cap.read()
    if not ok:
        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
        ok, frame = cap.read()
        if not ok or frame is None:
            raise PipelineError("Cannot read frame from video")
    return frame


def _letterbox(frame):
    """Aspect-preserving resize with padding; returns NCHW blob, scale, pad_x, pad_y."""
    h, w = frame.shape[:2]
    scale = min(YOLO_INPUT_SIZE / h, YOLO_INPUT_SIZE / w)
    nh, nw = int(h * scale), int(w * scale)
    resized = cv2.resize(frame, (nw, nh))
    canvas = np.full((YOLO_INPUT_SIZE, YOLO_INPUT_SIZE, 3),
                     LETTERBOX_PAD_VALUE, dtype=np.uint8)
    py, px = (YOLO_INPUT_SIZE - nh) // 2, (YOLO_INPUT_SIZE - nw) // 2
    canvas[py:py + nh, px:px + nw] = resized
    blob = (canvas.astype(np.float32) / 255.0).transpose(2, 0, 1)[np.newaxis]
    return blob, scale, px, py


def _decode_detections(output, scale, pad_x, pad_y):
    """Map YOLO26 output [1,300,6] back to original image coordinates."""
    return [
        (int((d[0] - pad_x) / scale), int((d[1] - pad_y) / scale),
         int((d[2] - pad_x) / scale), int((d[3] - pad_y) / scale),
         float(d[4]), int(d[5]))
        for d in output.squeeze(0) if float(d[4]) >= CONFIDENCE_THRESHOLD
    ]


def _draw_boxes(frame, dets, names):
    """Draw bounding boxes with class labels."""
    vis = frame.copy()
    h, w = vis.shape[:2]
    for x1, y1, x2, y2, score, cls in dets:
        x1, y1, x2, y2 = max(0, x1), max(0, y1), min(w, x2), min(h, y2)
        color = COLORS[cls % len(COLORS)]
        cv2.rectangle(vis, (x1, y1), (x2, y2), color, 2)
        label = f"{names.get(cls, str(cls))} {score:.0%}"
        text_size = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.55, 1)[0]
        ly = max(y1, text_size[1] + 8)
        cv2.rectangle(vis, (x1, ly - text_size[1] - 6), (x1 + text_size[0] + 4, ly), color, -1)
        cv2.putText(vis, label, (x1 + 2, ly - 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1, cv2.LINE_AA)
    return vis


def _stamp_frame(frame, text):
    """Semi-transparent timing banner at the top."""
    overlay = frame.copy()
    (tw, th), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.7, 2)
    cv2.rectangle(overlay, (8, 6), (tw + 16, th + 16), (0, 0, 0), -1)
    cv2.addWeighted(overlay, 0.5, frame, 0.5, 0, frame)
    cv2.putText(frame, text, (12, th + 10),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2, cv2.LINE_AA)
    return frame


def _create_infer_request(model_path):
    """Compile model on iGPU and return an infer request."""
    core = ov.Core()
    if INFERENCE_DEVICE not in core.available_devices:
        raise PipelineError(
            f"{INFERENCE_DEVICE} not available, found: {core.available_devices}")
    return core.compile_model(
        core.read_model(str(model_path)), INFERENCE_DEVICE).create_infer_request()


def run(model_path, video, num_frames, warmup) -> Result:
    """Synchronous OpenCV decode + OpenVINO iGPU inference loop.

    Measures wall-clock time after each inference. E2E time is the mean
    interval between consecutive wall-clock timestamps, which includes
    decode + preprocess + inference + postprocess for each frame.
    """
    req = _create_infer_request(model_path)
    cap = cv2.VideoCapture(str(video))
    if not cap.isOpened():
        raise PipelineError(f"Cannot open video: {video}")

    for _ in range(warmup):
        req.infer({0: _letterbox(_read_frame(cap))[0]})

    cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
    wall_times = []

    for _ in range(num_frames):
        blob, scale, px, py = _letterbox(_read_frame(cap))
        req.infer({0: blob})
        wall_times.append(time.monotonic())

    cap.release()
    return compute_result(wall_times)


def save_snapshot(model_path, video, names, path) -> None:
    """Save first frame with detections as annotated JPEG."""
    req = _create_infer_request(model_path)
    cap = cv2.VideoCapture(str(video))
    for _ in range(SNAPSHOT_FRAMES):
        frame = _read_frame(cap)
        blob, scale, px, py = _letterbox(frame)
        t0 = time.monotonic()
        req.infer({0: blob})
        dets = _decode_detections(req.get_output_tensor(0).data, scale, px, py)
        if dets:
            vis = _draw_boxes(frame, dets, names)
            _stamp_frame(vis, f"E2E via OpenCV + OpenVINO: {(time.monotonic() - t0) * 1000:.1f} ms")
            cv2.imwrite(str(path), vis)
            break
    cap.release()
