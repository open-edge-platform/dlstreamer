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

# shared_utils.py lives at samples/ root (three levels up from this script)
_samples_root = SCRIPT_DIR.parent.parent.parent
if not (_samples_root / "shared_utils.py").is_file():
    # Fallback for Docker image layout
    _samples_root = Path("/opt/intel/dlstreamer/samples")
sys.path.insert(0, str(_samples_root))

from shared_utils import download_https  # pylint: disable=wrong-import-position

DEFAULT_VIDEO_URL = "https://videos.pexels.com/video-files/2431853/2431853-hd_1920_1080_25fps.mp4"
DEFAULT_VIDEO_NAME = "2431853-hd_1920_1080_25fps.mp4"


# ── helpers ──────────────────────────────────────────────────────────────────


def parse_args():
    p = argparse.ArgumentParser(description="Smart NVR — Lane Hogging Detection")
    p.add_argument("--input", default=None, help="Video file path (default: auto-download Pexels sample)")
    p.add_argument("--model", default=str(SCRIPT_DIR / "models" / "rtdetr_v2_r50vd" / "model.xml"),
                   help="Path to detection model .xml (default: models/rtdetr_v2_r50vd/model.xml)")
    p.add_argument("--device", default="GPU", help="Inference device (default: GPU)")
    p.add_argument("--output", default="output.mp4", help="Output file location pattern (default: output.mp4)")
    p.add_argument("--max-time", type=int, default=10, help="Chunk duration in seconds (default: 10)")
    p.add_argument("--threshold", type=float, default=0.7, help="Detection confidence threshold (default: 0.7)")
    p.add_argument("--batch-size", type=int, default=4, help="Inference batch size (default: 4)")
    return p.parse_args()


def ensure_video(input_path):
    """Return path to input video, downloading default if none provided."""
    if input_path:
        path = Path(input_path)
        if not path.is_file():
            sys.stderr.write(f"Error: file not found: {input_path}\n")
            sys.exit(1)
        return str(path.resolve())

    video_path = SCRIPT_DIR / DEFAULT_VIDEO_NAME
    if not video_path.is_file():
        print("\nNo input provided. Downloading default video...\n")
        download_https(DEFAULT_VIDEO_URL, str(video_path), {"videos.pexels.com"})
    return str(video_path)


def ensure_model(model_path):
    """Return path to detection model; fail if not found."""
    model_xml = Path(model_path)
    if not model_xml.is_file():
        sys.stderr.write(
            f"Error: detection model not found at {model_path}.\n"
            "Export it first — see README.md, 'Prepare Model' section.\n"
        )
        sys.exit(1)
    return str(model_xml)


def run_pipeline(pipeline):
    """Run pipeline event loop with SIGINT → EOS for graceful shutdown."""

    def _sigint(signum, frame):
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
    args = parse_args()

    # Register custom Python elements
    plugins_dir = str(SCRIPT_DIR / "plugins")
    if plugins_dir not in os.environ.get("GST_PLUGIN_PATH", ""):
        # Keep only /opt/intel paths, excluding gstreamer-1.0 dir (its python/ subdir
        # contains gesotioformatter.py which conflicts with custom Gst.Bin subclasses)
        existing = os.environ.get("GST_PLUGIN_PATH", "")
        filtered = ":".join(
            p for p in existing.split(":")
            if p and p.startswith("/opt/intel") and "gstreamer-1.0" not in p
        )
        os.environ["GST_PLUGIN_PATH"] = f"{filtered}:{plugins_dir}"
    os.environ.setdefault("GST_REGISTRY_FORK", "no")

    Gst.init([])
    reg = Gst.Registry.get()
    if not reg.find_plugin("python"):
        raise RuntimeError(
            "GStreamer 'python' plugin not found. Install gst-python / "
            "python3-gst-1.0 and clear ~/.cache/gstreamer-1.0/registry.*.bin if needed."
        )

    # Prepare assets
    video_file = ensure_video(args.input)
    detection_model = ensure_model(args.model)

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
