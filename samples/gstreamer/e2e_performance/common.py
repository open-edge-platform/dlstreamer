# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Common infrastructure: model/video preparation, YOLO pre/postprocessing,
visualization, result container, and benchmarking harness."""

import statistics
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np

SCRIPT_DIR = Path(__file__).resolve().parent
MODEL_NAME = "yolo26s"
MODEL_DIR = SCRIPT_DIR / f"{MODEL_NAME}_int8_openvino_model"
MODEL_XML = MODEL_DIR / f"{MODEL_NAME}.xml"
OUTPUT_DIR = SCRIPT_DIR / "output"

VIDEO_URL = ("https://storage.openvinotoolkit.org/repositories/"
             "openvino_notebooks/data/data/video/people.mp4")
VIDEO_PATH = SCRIPT_DIR / "data" / "people.mp4"

YOLO_INPUT_SIZE = 640               # YOLO26s fixed input resolution
CONFIDENCE_THRESHOLD = 0.35
LETTERBOX_PAD_VALUE = 114            # standard YOLO gray padding
INFERENCE_DEVICE = "GPU"
NIREQ = 4                           # async slots for DLStreamer pipeline
QUEUE_SIZE = 8                       # GStreamer queue depth
SNAPSHOT_FRAMES = 90                 # frames for watermarked snapshot
RUN_COOLDOWN = 2                     # seconds between runs (thermal)

COLORS = [
    (56, 56, 255), (151, 157, 255), (31, 112, 255), (29, 178, 255),
    (49, 210, 207), (10, 249, 72), (23, 204, 146), (134, 219, 61),
    (182, 219, 61), (221, 111, 76),
]


class PipelineError(RuntimeError):
    """Raised when a pipeline encounters an unrecoverable condition."""


# -- assets -------------------------------------------------------------------

def prepare_model() -> Path:
    """Export YOLO26s to OpenVINO INT8 if not cached."""
    if MODEL_XML.exists():
        return MODEL_XML
    from ultralytics import YOLO  # pylint: disable=import-outside-toplevel
    print(f"Exporting {MODEL_NAME} to OpenVINO INT8 ...")
    out = Path(YOLO(MODEL_NAME).export(
        format="openvino", int8=True, imgsz=YOLO_INPUT_SIZE, dynamic=False))
    if out.resolve() != MODEL_DIR.resolve():
        MODEL_DIR.mkdir(parents=True, exist_ok=True)
        for f in out.iterdir():
            f.rename(MODEL_DIR / f.name)
    return MODEL_XML


def prepare_video() -> Path:
    """Download test video if not cached."""
    if VIDEO_PATH.exists():
        return VIDEO_PATH
    VIDEO_PATH.parent.mkdir(parents=True, exist_ok=True)
    print("Downloading test video ...")
    urllib.request.urlretrieve(VIDEO_URL, VIDEO_PATH)
    return VIDEO_PATH


def load_class_names() -> dict[int, str]:
    """Parse COCO class names from ultralytics metadata."""
    meta = MODEL_DIR / "metadata.yaml"
    if not meta.exists():
        return {}
    names: dict[int, str] = {}
    for line in meta.read_text().splitlines():
        stripped = line.strip()
        if stripped and stripped[0].isdigit() and ":" in stripped:
            key, val = stripped.split(":", 1)
            names[int(key.strip())] = val.strip()
    return names


# -- result -------------------------------------------------------------------

@dataclass(frozen=True, slots=True)
class Result:
    """Per-run measurement data."""
    fps: float
    e2e_ms: float
    frames: int

    def __str__(self) -> str:
        return f"{self.fps:.1f} fps  e2e={self.e2e_ms:.1f} ms  ({self.frames} frames)"


def compute_result(wall_times) -> Result:
    """Compute Result from raw wall-clock timestamps."""
    if len(wall_times) < 2:
        raise PipelineError("Too few frames measured")
    elapsed = wall_times[-1] - wall_times[0]
    fps = (len(wall_times) - 1) / elapsed if elapsed > 0 else 0
    diffs = [(wall_times[i] - wall_times[i - 1]) * 1000.0
             for i in range(1, len(wall_times))]
    return Result(fps=fps, e2e_ms=statistics.mean(diffs), frames=len(wall_times))


# -- YOLO pre/postprocessing --------------------------------------------------

def letterbox(frame: np.ndarray) -> tuple[np.ndarray, float, int, int]:
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


def decode_detections(output: np.ndarray, scale: float,
                      pad_x: int, pad_y: int) -> list[tuple]:
    """Map YOLO26 output [1,300,6] back to original image coordinates."""
    return [
        (int((d[0] - pad_x) / scale), int((d[1] - pad_y) / scale),
         int((d[2] - pad_x) / scale), int((d[3] - pad_y) / scale),
         float(d[4]), int(d[5]))
        for d in output.squeeze(0) if float(d[4]) >= CONFIDENCE_THRESHOLD
    ]


# -- visualization ------------------------------------------------------------

def draw_boxes(frame: np.ndarray, dets: list[tuple],
               names: dict[int, str]) -> np.ndarray:
    """Draw bounding boxes with class labels."""
    vis = frame.copy()
    h, w = vis.shape[:2]
    for x1, y1, x2, y2, score, cls in dets:
        x1, y1, x2, y2 = max(0, x1), max(0, y1), min(w, x2), min(h, y2)
        color = COLORS[cls % len(COLORS)]
        cv2.rectangle(vis, (x1, y1), (x2, y2), color, 2)
        label = f"{names.get(cls, str(cls))} {score:.0%}"
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.55, 1)
        ly = max(y1, th + 8)
        cv2.rectangle(vis, (x1, ly - th - 6), (x1 + tw + 4, ly), color, -1)
        cv2.putText(vis, label, (x1 + 2, ly - 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1, cv2.LINE_AA)
    return vis


def stamp_frame(frame: np.ndarray, text: str) -> np.ndarray:
    """Semi-transparent timing banner at the top."""
    overlay = frame.copy()
    (tw, th), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.7, 2)
    cv2.rectangle(overlay, (8, 6), (tw + 16, th + 16), (0, 0, 0), -1)
    cv2.addWeighted(overlay, 0.5, frame, 0.5, 0, frame)
    cv2.putText(frame, text, (12, th + 10),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2, cv2.LINE_AA)
    return frame


def read_frame(cap: cv2.VideoCapture) -> np.ndarray:
    """Read next frame, loop if video ends."""
    ok, frame = cap.read()
    if not ok:
        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
        ok, frame = cap.read()
        if not ok or frame is None:
            raise PipelineError("Cannot read frame from video")
    return frame


# -- benchmarking harness -----------------------------------------------------

def run_benchmark(label, run_fn, model, video, num_frames, warmup, runs) -> list[Result]:
    """Run a pipeline multiple times and print per-run results."""
    print(label)
    results: list[Result] = []
    for i in range(runs):
        result = run_fn(model, video, num_frames, warmup)
        results.append(result)
        print(f"  run {i + 1}: {result}")
        time.sleep(RUN_COOLDOWN)
    return results
