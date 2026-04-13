# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
DL Streamer <APPLICATION_NAME> pipeline.

Pipeline:
    filesrc → decodebin3 →
    gvadetect → gvafpscounter → gvawatermark →
    gvametaconvert → gvametapublish (JSON Lines) →
    videoconvert → vah264enc → h264parse → mp4mux → filesink

Supports file, HTTP URL, and RTSP IP camera inputs.
"""

import argparse
import os
import signal
import sys
import urllib.request
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst  # pylint: disable=no-name-in-module, wrong-import-position

SCRIPT_DIR = Path(__file__).resolve().parent
MODELS_DIR = SCRIPT_DIR / "models"
VIDEOS_DIR = SCRIPT_DIR / "videos"
RESULTS_DIR = SCRIPT_DIR / "results"

DEFAULT_VIDEO_URL = "<VIDEO_URL>"


# ── helpers ──────────────────────────────────────────────────────────────────


def detect_devices():
    """Detect available Intel accelerators on the system."""
    devices = ["CPU"]  # CPU is always available
    if any(os.path.exists(f"/dev/dri/renderD{128 + i}") for i in range(16)):
        devices.append("GPU")
    if any(os.path.exists(f"/dev/accel/accel{i}") for i in range(8)):
        devices.append("NPU")
    return devices


def parse_args():
    available = detect_devices()
    default_device = "GPU" if "GPU" in available else "CPU"

    p = argparse.ArgumentParser(description="DL Streamer <APPLICATION_NAME>")
    p.add_argument(
        "--input",
        default=DEFAULT_VIDEO_URL,
        help="Video file path, HTTP URL, or rtsp:// URI",
    )
    p.add_argument(
        "--device",
        default=default_device,
        choices=["CPU", "GPU", "NPU"],
        help=f"Inference device (default: {default_device}, available: {available})",
    )
    p.add_argument("--batch-size", type=int, default=0,
                   help="Inference batch size (0 = auto: GPU=4, NPU=1, CPU=2)")
    p.add_argument("--output-video", default=str(RESULTS_DIR / "output.mp4"))
    p.add_argument("--output-json", default=str(RESULTS_DIR / "results.jsonl"))
    p.add_argument("--threshold", type=float, default=0.5, help="Detection threshold")
    return p.parse_args()


def prepare_input(source: str) -> str:
    """Download video if HTTP URL; pass through for RTSP or local file."""
    if source.startswith("rtsp://"):
        return source
    if source.startswith(("http://", "https://")):
        VIDEOS_DIR.mkdir(parents=True, exist_ok=True)
        name = source.rstrip("/").split("/")[-1]
        local = VIDEOS_DIR / name
        if not local.exists():
            print(f"Downloading video: {source}")
            req = urllib.request.Request(source, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=120) as r:  # noqa: S310
                local.write_bytes(r.read())
            print(f"Saved to: {local}")
        return str(local)
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


def build_source(src: str) -> str:
    """Build GStreamer source element string for file or RTSP."""
    if src.startswith("rtsp://"):
        return f"rtspsrc location={src} latency=100"
    return f'filesrc location="{src}"'


def run_pipeline(pipeline):
    """Event loop with SIGINT → EOS for graceful RTSP shutdown."""

    def _sigint(signum, frame):
        pipeline.send_event(Gst.Event.new_eos())

    prev = signal.signal(signal.SIGINT, _sigint)
    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)
    try:
        while True:
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

    # Prepare input
    input_src = prepare_input(args.input)

    # Locate models (adjust glob patterns for your models)
    model_xml = find_model("**/*.xml", "detection")

    # Output dirs
    Path(args.output_video).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output_json).parent.mkdir(parents=True, exist_ok=True)

    # Device validation and fallback
    available = detect_devices()
    device = args.device
    if device not in available:
        fallback = "CPU"
        print(f"Warning: {device} not available, falling back to {fallback}")
        device = fallback

    # Auto-select batch size based on device if not specified
    batch_size = args.batch_size
    if batch_size == 0:
        batch_size = {"GPU": 4, "NPU": 1, "CPU": 2}.get(device, 2)

    # Build and run pipeline
    Gst.init(None)
    source_el = build_source(input_src)

    pipe = (
        f"{source_el} ! decodebin3 ! "
        f'gvadetect model="{model_xml}" device={device} '
        f"batch-size={batch_size} threshold={args.threshold} ! queue ! "
        f"gvafpscounter ! gvawatermark ! "
        f"gvametaconvert ! "
        f'gvametapublish file-format=json-lines file-path="{args.output_json}" ! '
        f"videoconvert ! vah264enc ! h264parse ! mp4mux ! "
        f'filesink location="{args.output_video}"'
    )

    print(f"\nPipeline:\n{pipe}\n")
    pipeline = Gst.parse_launch(pipe)
    run_pipeline(pipeline)

    print(f"\nOutput video: {args.output_video}")
    print(f"Output JSON:  {args.output_json}")


if __name__ == "__main__":
    main()
