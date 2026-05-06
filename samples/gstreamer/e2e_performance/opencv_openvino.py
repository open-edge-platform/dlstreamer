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
import openvino as ov

from common import (INFERENCE_DEVICE, SNAPSHOT_FRAMES, PipelineError, Result,
                     compute_result, decode_detections, draw_boxes, letterbox,
                     read_frame, stamp_frame)


def run(model_path, video, num_frames, warmup) -> Result:
    """Synchronous OpenCV decode + OpenVINO iGPU inference loop."""
    core = ov.Core()
    if INFERENCE_DEVICE not in core.available_devices:
        raise PipelineError(
            f"{INFERENCE_DEVICE} not available, found: {core.available_devices}")
    compiled = core.compile_model(core.read_model(str(model_path)), INFERENCE_DEVICE)
    req = compiled.create_infer_request()
    cap = cv2.VideoCapture(str(video))
    if not cap.isOpened():
        raise PipelineError(f"Cannot open video: {video}")

    for _ in range(warmup):
        req.infer({0: letterbox(read_frame(cap))[0]})

    cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
    wall_times = []

    for _ in range(num_frames):
        blob, scale, px, py = letterbox(read_frame(cap))
        req.infer({0: blob})
        wall_times.append(time.monotonic())
        decode_detections(req.get_output_tensor(0).data, scale, px, py)

    cap.release()
    return compute_result(wall_times)


def save_snapshot(model_path, video, names, path) -> None:
    """Run a few frames and save one annotated detection image."""
    core = ov.Core()
    compiled = core.compile_model(core.read_model(str(model_path)), INFERENCE_DEVICE)
    req = compiled.create_infer_request()
    cap = cv2.VideoCapture(str(video))
    for _ in range(SNAPSHOT_FRAMES):
        frame = read_frame(cap)
        blob, scale, px, py = letterbox(frame)
        t0 = time.monotonic()
        req.infer({0: blob})
        dets = decode_detections(req.get_output_tensor(0).data, scale, px, py)
        if dets:
            vis = draw_boxes(frame, dets, names)
            stamp_frame(vis, f"E2E via OpenCV + OpenVINO: {(time.monotonic() - t0) * 1000:.1f} ms")
            cv2.imwrite(str(path), vis)
            break
    cap.release()
