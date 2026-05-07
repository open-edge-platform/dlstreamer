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

YOLO_INPUT_SIZE = 640
CONFIDENCE_THRESHOLD = 0.35
LETTERBOX_PAD_VALUE = 114
INFERENCE_DEVICE = "GPU"
NIREQ = 4
QUEUE_SIZE = 8
SNAPSHOT_FRAMES = 90
RUN_COOLDOWN = 2


class PipelineError(RuntimeError):
    """Raised when a pipeline encounters an unrecoverable condition."""


@dataclass(frozen=True, slots=True)
class Result:
    """Per-run measurement data."""
    fps: float
    inference_ms: float
    frames: int

    def __str__(self) -> str:
        return f"{self.fps:.1f} fps  {self.inference_ms:.1f} ms/frame  ({self.frames} frames)"


def compute_throughput(wall_times: list[float]) -> Result:
    """Compute throughput from wall-clock timestamps at pipeline output."""
    if len(wall_times) < 2:
        raise PipelineError("Too few frames measured")
    n = len(wall_times)
    elapsed = wall_times[-1] - wall_times[0]
    fps = (n - 1) / elapsed if elapsed > 0 else 0.0
    mean_interval = elapsed / (n - 1) * 1000.0
    return Result(fps=fps, inference_ms=mean_interval, frames=n)


def prepare_model() -> Path:
    """Export YOLO26s to OpenVINO INT8 if not cached."""
    if MODEL_XML.exists():
        return MODEL_DIR
    from ultralytics import YOLO  # pylint: disable=import-outside-toplevel
    print(f"Exporting {MODEL_NAME} to OpenVINO INT8 ...")
    exported = Path(YOLO(MODEL_NAME).export(
        format="openvino", int8=True, imgsz=YOLO_INPUT_SIZE, dynamic=False))
    if exported.resolve() != MODEL_DIR.resolve():
        MODEL_DIR.mkdir(parents=True, exist_ok=True)
        for f in exported.iterdir():
            f.rename(MODEL_DIR / f.name)
    return MODEL_DIR


def prepare_video() -> Path:
    """Download test video if not cached."""
    if VIDEO_PATH.exists():
        return VIDEO_PATH
    VIDEO_PATH.parent.mkdir(parents=True, exist_ok=True)
    print("Downloading test video ...")
    urllib.request.urlretrieve(VIDEO_URL, VIDEO_PATH)
    return VIDEO_PATH


def _benchmark(label: str, run_fn, model, video, frames, warmup, runs) -> list[Result]:
    """Run a pipeline multiple times and print per-run results."""
    print(label)
    results: list[Result] = []
    for i in range(runs):
        r = run_fn(model, video, frames, warmup)
        results.append(r)
        print(f"  run {i + 1}: {r}")
        time.sleep(RUN_COOLDOWN)
    return results


def main() -> None:
    """Entry point."""
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--video", type=Path, default=None)
    ap.add_argument("--model", type=Path, default=None)
    ap.add_argument("--frames", type=int, default=200)
    ap.add_argument("--warmup", type=int, default=50)
    ap.add_argument("--runs", type=int, default=3)
    args = ap.parse_args()

    model_dir = args.model or prepare_model()
    video = args.video or prepare_video()
    model_xml = model_dir / f"{MODEL_NAME}.xml"
    if not model_xml.exists():
        raise FileNotFoundError(f"Model not found: {model_xml}")
    if not video.exists():
        raise FileNotFoundError(f"Video not found: {video}")
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    import opencv_openvino  # pylint: disable=import-outside-toplevel
    import dlstreamer  # pylint: disable=import-outside-toplevel

    print(f"Model : {model_dir}")
    print(f"Video : {video}")
    print(f"Config: {args.frames} frames + {args.warmup} warmup x {args.runs} runs\n")

    ov_results = _benchmark(
        "OpenCV + OpenVINO (notebook approach, iGPU inference)",
        opencv_openvino.run, model_dir, video,
        args.frames, args.warmup, args.runs)
    opencv_openvino.save_snapshot(model_dir, video, OUTPUT_DIR / "opencv_openvino_detection.jpg")

    dls_results = _benchmark(
        "\nDLStreamer (iGPU decode, zero-copy, async nireq=4)",
        dlstreamer.run, model_xml, video,
        args.frames, args.warmup, args.runs)
    dlstreamer.save_snapshot(model_xml, video, OUTPUT_DIR / "dlstreamer_detection.jpg")

    ov_fps = statistics.mean(r.fps for r in ov_results)
    dls_fps = statistics.mean(r.fps for r in dls_results)
    ov_ms = statistics.mean(r.inference_ms for r in ov_results)
    dls_ms = statistics.mean(r.inference_ms for r in dls_results)
    advantage = (dls_fps / ov_fps - 1) * 100 if ov_fps else 0

    sep = "-" * 64
    print(f"\n{sep}")
    print(f"  OpenCV+OV (iGPU) : {ov_fps:>7.1f} fps   {ov_ms:.1f} ms/frame")
    print(f"  DLStreamer (iGPU): {dls_fps:>7.1f} fps   {dls_ms:.1f} ms/frame")
    print(sep)
    print(f"  DLStreamer advantage: up to {advantage:.0f}% higher throughput")
    print(f"  Detection output: {OUTPUT_DIR}")
    print(sep)


if __name__ == "__main__":
    main()
