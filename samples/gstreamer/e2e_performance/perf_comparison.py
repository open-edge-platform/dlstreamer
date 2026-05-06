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
from pathlib import Path

import opencv_openvino
import dlstreamer
from common import (OUTPUT_DIR, prepare_model, prepare_video,
                     load_class_names, run_benchmark)


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

    print(f"Model : {model_xml}")
    print(f"Video : {video}")
    print(f"Config: {args.frames} frames + {args.warmup} warmup x {args.runs} runs\n")

    ov_results = run_benchmark(
        "OpenCV + OpenVINO pipeline (iGPU inference)",
        opencv_openvino.run, model_xml, video,
        args.frames, args.warmup, args.runs)

    dls_results = run_benchmark(
        "\nDLStreamer pipeline (iGPU decode, zero-copy, async inference)",
        dlstreamer.run, model_xml, video,
        args.frames, args.warmup, args.runs)

    ov_fps = statistics.mean(r.fps for r in ov_results)
    dls_fps = statistics.mean(r.fps for r in dls_results)
    ov_e2e = statistics.mean(r.e2e_ms for r in ov_results)
    dls_e2e = statistics.mean(r.e2e_ms for r in dls_results)
    opencv_openvino.save_snapshot(
        model_xml, video, names, OUTPUT_DIR / "opencv_openvino_detection.jpg")
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
