# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
DLStreamer <APPLICATION_NAME> pipeline.

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
import subprocess
import sys
from pathlib import Path, PurePosixPath
from urllib.parse import urlparse

import gi

gi.require_version("Gst", "1.0")

# Prevent GStreamer from forking gst-plugin-scanner (a C subprocess that cannot
# resolve Python symbols). Scanning in-process lets libgstpython.so find the
# Python runtime that is already loaded.
os.environ.setdefault("GST_REGISTRY_FORK", "no")

from gi.repository import GLib, Gst  # pylint: disable=no-name-in-module, wrong-import-position

SCRIPT_DIR = Path(__file__).resolve().parent
MODELS_DIR = SCRIPT_DIR / "models"
VIDEOS_DIR = SCRIPT_DIR / "videos"
RESULTS_DIR = SCRIPT_DIR / "results"

DEFAULT_VIDEO_URL = "<VIDEO_URL>"


# ── helpers ──────────────────────────────────────────────────────────────────


def parse_args():
    p = argparse.ArgumentParser(description="DLStreamer <APPLICATION_NAME>")
    p.add_argument(
        "--input",
        default=DEFAULT_VIDEO_URL,
        help="Video file path, HTTP URL, or rtsp:// URI",
    )
    p.add_argument("--device", default="GPU", help="Inference device (default: GPU)")
    p.add_argument("--output-video", default=str(RESULTS_DIR / "output.mp4"))
    p.add_argument("--output-json", default=str(RESULTS_DIR / "results.jsonl"))
    return p.parse_args()


def prepare_input(source: str) -> str:
    """Download video if HTTP URL; pass through for RTSP or local file."""
    if source.startswith("rtsp://"):
        return source
    if source.startswith(("http://", "https://")):
        VIDEOS_DIR.mkdir(parents=True, exist_ok=True)
        name = PurePosixPath(urlparse(source).path).name or "video.mp4"
        local = VIDEOS_DIR / name
        if not local.exists():
            print(f"Downloading video: {source}")
            subprocess.run([
                "curl", "-L", "-o", str(local),
                "-H", "Referer: https://www.pexels.com/",
                "-H", "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36",
                source,
            ], check=True, timeout=300)
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
    """Event loop with SIGINT → EOS for graceful RTSP shutdown."""

    # [Optional] For long-running pipelines, add SIGINT → EOS handler
    # to set pipeline.set_state(Gst.State.NULL) for immediate stop.
    def _sigint(signum, frame):
        pipeline.send_event(Gst.Event.new_eos())

    prev = signal.signal(signal.SIGINT, _sigint)
    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)
    try:
        while True:
            # [Optional] Add when application processes user commands or
            # any thread-safe dispatch via GLib.idle_add(). No-op otherwise.
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

    # Prepare input
    input_src = prepare_input(args.input)

    # Locate models (adjust glob patterns for your models)
    model_xml = find_model("**/*.xml", "detection")

    # Output dirs
    Path(args.output_video).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output_json).parent.mkdir(parents=True, exist_ok=True)

    # Device fallback
    device = check_device(args.device, "inference")

    # Build and run pipeline
    Gst.init(None)
    source_el = build_source(input_src)

    pipe = (
        f"{source_el} ! decodebin3 ! "
        f'gvadetect model="{model_xml}" device={device} '
        f"batch-size=4 threshold={args.threshold} ! queue ! "
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
