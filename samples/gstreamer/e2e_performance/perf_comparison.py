#!/usr/bin/env python3
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""DL Streamer vs OpenCV + OpenVINO E2E performance comparison.

    python3 perf_comparison.py [--frames N] [--warmup N] [--runs N]
"""

import argparse
import statistics
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path

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


@dataclass(frozen=True, slots=True)
class Result:
    """Per-run measurement data."""
    fps: float
    e2e_ms: float
    frames: int

    def __str__(self) -> str:
        return f"{self.fps:.1f} fps  e2e={self.e2e_ms:.1f} ms  ({self.frames} frames)"


def compute_result(wall_times) -> Result:
    """Compute Result from wall-clock timestamps recorded after each frame.

    fps = (N-1) / (t_last - t_first), where N is the number of timestamps.
    e2e_ms = mean of consecutive timestamp intervals in milliseconds.
    This measures the sustained per-frame throughput of the pipeline.
    """
    if len(wall_times) < 2:
        raise PipelineError("Too few frames measured")
    elapsed = wall_times[-1] - wall_times[0]
    fps = (len(wall_times) - 1) / elapsed if elapsed > 0 else 0
    diffs = [(wall_times[i] - wall_times[i - 1]) * 1000.0
             for i in range(1, len(wall_times))]
    return Result(fps=fps, e2e_ms=statistics.mean(diffs), frames=len(wall_times))


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


def _run_benchmark(label, run_fn, model, video, num_frames, warmup, runs):
    """Run a pipeline multiple times and print per-run results."""
    print(label)
    results = []
    for i in range(runs):
        result = run_fn(model, video, num_frames, warmup)
        results.append(result)
        print(f"  run {i + 1}: {result}")
        time.sleep(RUN_COOLDOWN)
    return results


def main() -> None:
    """Run both pipelines and print comparison."""
    parser = argparse.ArgumentParser(
        description="DL Streamer vs OpenCV + OpenVINO E2E performance comparison")
    parser.add_argument("--video", type=Path, default=None)
    parser.add_argument("--model", type=Path, default=None)
    parser.add_argument("--frames", type=int, default=200)
    parser.add_argument("--warmup", type=int, default=50)
    parser.add_argument("--runs", type=int, default=3)
    args = parser.parse_args()

    model_xml = args.model or prepare_model()
    video = args.video or prepare_video()
    if not model_xml.exists():
        raise FileNotFoundError(f"Model not found: {model_xml}")
    if not video.exists():
        raise FileNotFoundError(f"Video not found: {video}")
    names = load_class_names()
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    import opencv_openvino  # pylint: disable=import-outside-toplevel
    import dlstreamer  # pylint: disable=import-outside-toplevel

    print(f"Model : {model_xml}")
    print(f"Video : {video}")
    print(f"Config: {args.frames} frames + {args.warmup} warmup x {args.runs} runs\n")

    ov_results = _run_benchmark(
        "OpenCV + OpenVINO pipeline (iGPU inference)",
        opencv_openvino.run, model_xml, video,
        args.frames, args.warmup, args.runs)

    opencv_openvino.save_snapshot(
        model_xml, video, names, OUTPUT_DIR / "opencv_openvino_detection.jpg")

    dls_results = _run_benchmark(
        "\nDLStreamer pipeline (iGPU decode, zero-copy, async inference)",
        dlstreamer.run, model_xml, video,
        args.frames, args.warmup, args.runs)

    ov_fps = statistics.mean(r.fps for r in ov_results)
    dls_fps = statistics.mean(r.fps for r in dls_results)
    ov_e2e = statistics.mean(r.e2e_ms for r in ov_results)
    dls_e2e = statistics.mean(r.e2e_ms for r in dls_results)

    dlstreamer.save_snapshot(
        model_xml, video, OUTPUT_DIR / "dlstreamer_detection.jpg", e2e_ms=dls_e2e)

    tp = (dls_fps - ov_fps) / ov_fps * 100 if ov_fps else 0
    lp = (ov_e2e - dls_e2e) / ov_e2e * 100 if ov_e2e else 0

    sep = "-" * 64
    print(f"\n{sep}")
    print(f"  OpenCV+OV  : {ov_fps:>7.1f} fps   e2e = {ov_e2e:.1f} ms")
    print(f"  DLStreamer : {dls_fps:>7.1f} fps   e2e = {dls_e2e:.1f} ms")
    print(sep)
    print("  DLStreamer advantage on ARL/PTL:")
    print(f"  Up to {tp:.0f}% higher throughput, {lp:.0f}% lower e2e latency")
    print(f"\n  Detection output: {OUTPUT_DIR}")
    print(sep)


if __name__ == "__main__":
    main()
