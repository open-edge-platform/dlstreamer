# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
This sample application demonstrates how to add custom Python elements to DLStreamer pipeline.
- gvaanalytics_py analyzes bounding-box detection results and identifies cars
  hogging lane in a predefined inspection zone.
- gvarecorder_py splits the video stream into N-second file chunks and stores
  custom detection metadata along with each chunk.
"""

import argparse
import os
import signal
import sys
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")
from gi.repository import GLib, Gst  # pylint: disable=no-name-in-module,wrong-import-order,wrong-import-position

SCRIPT_DIR = Path(__file__).resolve().parent


# ── helpers ──────────────────────────────────────────────────────────────────


def parse_args():
    """Parse command-line arguments."""
    p = argparse.ArgumentParser(description="Smart NVR — Lane Hogging Detection")
    p.add_argument("--input", required=True, help="Path to input video file")
    p.add_argument("--model", required=True, help="Path to detection model .xml")
    p.add_argument("--device", default="GPU", help="Inference device (default: GPU)")
    p.add_argument("--output", default="output.mp4", help="Output file location pattern (default: output.mp4)")
    p.add_argument("--max-time", type=int, default=10, help="Chunk duration in seconds (default: 10)")
    p.add_argument("--threshold", type=float, default=0.7, help="Detection confidence threshold (default: 0.7)")
    p.add_argument("--batch-size", type=int, default=4, help="Inference batch size (default: 4)")
    return p.parse_args()


def ensure_file(path, description):
    """Return resolved path if file exists; exit with error otherwise."""
    p = Path(path)
    if not p.is_file():
        sys.stderr.write(f"Error: {description} not found: {path}\n")
        sys.exit(1)
    return str(p.resolve())


def run_pipeline(pipeline):
    """Run pipeline event loop with SIGINT → EOS for graceful shutdown."""

    def _sigint(_signum, _frame):
        pipeline.send_event(Gst.Event.new_eos())

    prev = signal.signal(signal.SIGINT, _sigint)
    bus = pipeline.get_bus()

    pipeline.set_state(Gst.State.PLAYING)
    try:
        while True:
            while GLib.MainContext.default().iteration(False):
                pass

            msg = bus.timed_pop_filtered(
                100 * Gst.MSECOND,
                Gst.MessageType.ERROR | Gst.MessageType.EOS,
            )
            if msg is None:
                continue
            if msg.type == Gst.MessageType.ERROR:
                err, debug = msg.parse_error()
                raise RuntimeError(f"Pipeline error: {err.message}\nDebug: {debug}")
            if msg.type == Gst.MessageType.EOS:
                print("Pipeline complete.")
                break
    finally:
        signal.signal(signal.SIGINT, prev)
        pipeline.set_state(Gst.State.NULL)


# ── main ─────────────────────────────────────────────────────────────────────


def main():
    """Entry point: build and run the Smart NVR pipeline."""
    args = parse_args()

    # Register custom Python elements
    plugins_dir = str(SCRIPT_DIR / "plugins")
    if plugins_dir not in os.environ.get("GST_PLUGIN_PATH", ""):
        existing = os.environ.get("GST_PLUGIN_PATH", "")
        os.environ["GST_PLUGIN_PATH"] = f"{existing}:{plugins_dir}" if existing else plugins_dir
    os.environ.setdefault("GST_REGISTRY_FORK", "no")

    Gst.init([])
    reg = Gst.Registry.get()
    if not reg.find_plugin("python"):
        raise RuntimeError(
            "GStreamer 'python' plugin not found. Install gst-python / "
            "python3-gst-1.0 and clear ~/.cache/gstreamer-1.0/registry.*.bin if needed."
        )

    # Prepare assets
    video_file = ensure_file(args.input, "input video")
    detection_model = ensure_file(args.model, "detection model")

    # Build pipeline
    pipe = (
        f'filesrc location="{video_file}" ! decodebin3 caps="video/x-raw(ANY)" ! '
        f'gvadetect model="{detection_model}" device={args.device} '
        f"batch-size={args.batch_size} threshold={args.threshold} ! queue ! "
        f"gvaanalytics_py distance=500 angle=-135,-45 ! gvafpscounter ! gvawatermark ! "
        f'gvarecorder_py location="{args.output}" max-time={args.max_time}'
    )
    print(f'Pipeline: "{pipe}"')
    pipeline = Gst.parse_launch(pipe)

    run_pipeline(pipeline)


if __name__ == "__main__":
    main()
