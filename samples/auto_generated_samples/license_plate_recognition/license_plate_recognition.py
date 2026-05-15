# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
DL Streamer License Plate Recognition pipeline.

Pipeline:
    filesrc → decodebin3 →
    gvadetect (YOLOv11 license plate detection) → queue →
    gvaclassify (PaddleOCR text recognition) → queue →
    gvafpscounter → gvawatermark →
    gvametaconvert → gvametapublish (JSON Lines) →
    videoconvert → vah264enc → h264parse → mp4mux → filesink

Supports file and RTSP IP camera inputs.
"""

import argparse
import os
import signal
import sys
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")

from gi.repository import GLib, Gst  # pylint: disable=no-name-in-module, wrong-import-position

SCRIPT_DIR = Path(__file__).resolve().parent
MODELS_DIR = SCRIPT_DIR / "models"
RESULTS_DIR = SCRIPT_DIR / "results"

DEFAULT_VIDEO = str(SCRIPT_DIR / "videos" / "ParkingVideo.mp4")


# ── helpers ──────────────────────────────────────────────────────────────────


def parse_args():
    p = argparse.ArgumentParser(description="DL Streamer License Plate Recognition")
    p.add_argument(
        "--input",
        default=DEFAULT_VIDEO,
        help="Video file path or rtsp:// URI",
    )
    p.add_argument("--detect-device", default="GPU", help="Device for detection (default: GPU)")
    p.add_argument("--ocr-device", default="GPU", help="Device for OCR (default: GPU)")
    p.add_argument("--output-video", default=str(RESULTS_DIR / "output.mp4"))
    p.add_argument("--output-json", default=str(RESULTS_DIR / "results.jsonl"))
    p.add_argument("--threshold", type=float, default=0.5, help="Detection confidence threshold")
    return p.parse_args()


def validate_input(source: str) -> str:
    """Validate video input path or RTSP URI."""
    if source.startswith("rtsp://"):
        return source
    if not os.path.isfile(source):
        sys.stderr.write(f"Error: file not found: {source}\n")
        sys.exit(1)
    return os.path.abspath(source)


def find_model(pattern: str, label: str) -> str:
    """Glob for a model .xml inside MODELS_DIR."""
    hits = sorted(MODELS_DIR.glob(pattern))
    if not hits:
        sys.stderr.write(f"Error: {label} model not found. Run: python3 export_models.py\n")
        sys.exit(1)
    return str(hits[0])


def check_device(requested: str, label: str) -> str:
    """Check device availability with fallback chain: NPU → GPU → CPU."""
    if requested == "NPU" and not os.path.exists("/dev/accel/accel0"):
        print(f"Warning: NPU not available for {label}, falling back to GPU")
        requested = "GPU"
    if requested == "GPU" and not os.path.exists("/dev/dri/renderD128"):
        print(f"Warning: GPU not available for {label}, falling back to CPU")
        requested = "CPU"
    return requested


def build_source(src: str) -> str:
    """Build GStreamer source element string for file or RTSP."""
    if src.startswith("rtsp://"):
        return f"rtspsrc location={src} latency=100"
    return f'filesrc location="{src}"'


def run_pipeline(pipeline):
    """Event loop with SIGINT → EOS for graceful shutdown."""

    def _sigint(signum, frame):
        pipeline.send_event(Gst.Event.new_eos())

    prev = signal.signal(signal.SIGINT, _sigint)
    bus = pipeline.get_bus()
    print("[pipeline] Compiling models, this may take some time...")
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
                err, dbg = msg.parse_error()
                print(f"Error from {msg.src.get_name()}: {err.message}\nDebug: {dbg}")
                break
            if msg.type == Gst.MessageType.EOS:
                print("Pipeline complete.")
                break
    finally:
        signal.signal(signal.SIGINT, prev)
        pipeline.set_state(Gst.State.NULL)


# ── main ─────────────────────────────────────────────────────────────────────


def main():
    args = parse_args()

    # Validate input
    input_src = validate_input(args.input)

    # Locate models
    detect_model = find_model("**/license-plate-finetune*.xml", "detection")
    ocr_model = find_model("**/PP-OCRv5_server_rec.xml", "OCR")

    # Output dirs
    Path(args.output_video).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output_json).parent.mkdir(parents=True, exist_ok=True)

    # Device fallback
    detect_device = check_device(args.detect_device, "detection")
    ocr_device = check_device(args.ocr_device, "OCR")

    # Build and run pipeline
    Gst.init(None)
    source_el = build_source(input_src)

    pipe = (
        f'{source_el} ! decodebin3 caps="video/x-raw(ANY)" ! '
        f'gvadetect model="{detect_model}" device={detect_device} '
        f"batch-size=4 threshold={args.threshold} ! queue ! "
        f'gvaclassify model="{ocr_model}" device={ocr_device} '
        f"batch-size=4 ! queue ! "
        f"gvafpscounter ! gvawatermark ! "
        f"gvametaconvert ! "
        f'gvametapublish file-format=json-lines file-path="{args.output_json}" ! '
        f"videoconvert ! vah264enc ! h264parse ! "
        f"mp4mux fragment-duration=1000 ! "
        f'filesink location="{args.output_video}"'
    )

    print(f"\nPipeline:\n{pipe}\n")
    pipeline = Gst.parse_launch(pipe)
    run_pipeline(pipeline)

    print(f"\nOutput video: {args.output_video}")
    print(f"Output JSON:  {args.output_json}")


if __name__ == "__main__":
    main()
