# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#!/usr/bin/env python3
"""
DLStreamer Loss Prevention sample.

Builds a pipeline that:
1. Reads a video file and decodes with decodebin3
2. Detects objects with a YOLO11 model (gvadetect)
3. Implements custom frame selection logic in gvaLossPrevention python element
"""

import argparse
import os
import signal
import sys
import urllib.request
from pathlib import Path

from ultralytics import YOLO

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib  # pylint: disable=no-name-in-module, wrong-import-position

BASE_DIR = Path(__file__).resolve().parent
VIDEOS_DIR = BASE_DIR / "videos"
MODELS_DIR = BASE_DIR / "models"
RESULTS_DIR = BASE_DIR / "results"
DEFAULT_VIDEO_URL = (
    "https://www.pexels.com/download/video/35256160"
)

def download_video(video_url: str) -> Path:
    """Return a local video path, downloading from URL if needed."""
    VIDEOS_DIR.mkdir(parents=True, exist_ok=True)
    filename = video_url.rstrip("/").split("/")[-1]
    if not os.path.splitext(filename)[1]:
        filename += ".mp4"

    local_path = VIDEOS_DIR / filename
    if not local_path.exists():
        request = urllib.request.Request(video_url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read()
            if not data:
                raise RuntimeError("Video download returned empty response")
            with open(local_path, "wb") as fh:
                fh.write(data)

    return local_path.resolve()


def download_detection_model(model_id: str) -> Path:
    """Return a path to the YOLO OpenVINO IR model, exporting if needed."""
    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    model_path = Path(f"{MODELS_DIR}/{model_id}_openvino_model/{model_id}.xml")

    if not model_path.exists():
        print(f"[detect] exporting {model_id} to OpenVINO format")
        model = YOLO(f"{model_id}.pt")
        exported_path = Path(model.export(format="openvino", dynamic=True, int8=True, data="coco128"))
        if exported_path.resolve() != model_path.parent.resolve():
            exported_path.parent.rename(model_path.parent)

    return model_path.resolve()


def construct_pipeline(
    video_file: Path,
    model_file: Path,
    detect_device: str,
    threshold: float
) -> str:
    """Construct the GStreamer pipeline string and return output paths."""
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    output_video = RESULTS_DIR / f"loss_prevention-{video_file.stem}.mp4"
    output_files = RESULTS_DIR / f"loss_prevention-{video_file.stem}-%d.jpeg"

    pipeline_str = (
        # Source → decode → detect
        f'filesrc location="{video_file}" ! '
        f'decodebin3 ! '
        f'gvadetect model="{model_file}" '
        f'device={detect_device} '
        f'threshold={threshold} ! '
        f'queue ! '
        f'gvatrack tracking-type=zero-term-imageless ! '
        f'gvawatermark ! '
        f'tee name=detect_tee '

        # # # Path 1: recording — save detected video to file
        f'detect_tee. ! '
        f'queue ! '
        f'gvafpscounter ! '
        f'videoconvert ! '
        f'vah264enc ! '
        f'h264parse ! '
        f'mp4mux ! '
        f'filesink location="{output_video}" '

        # Path 2: analytics — loss prevention analysis + save snapshots
        f'detect_tee. ! '
        f'queue ! '
        f'videoconvert ! video/x-raw,format=RGB ! '
        f'gvalossprevention_py threshold=50 ! '
        f'jpegenc ! '
        f'multifilesink location="{output_files}"'
    )

    print(f"[construct_pipeline] Pipeline: {pipeline_str}")

    return pipeline_str


def setup_gst_plugins() -> None:
    """Register local Python plugin path and initialise GStreamer."""
    plugins_dir = str(BASE_DIR / "plugins")
    if plugins_dir not in os.environ.get("GST_PLUGIN_PATH", ""):
        print(f'[plugins] adding "{plugins_dir}" to GST_PLUGIN_PATH')
        os.environ["GST_PLUGIN_PATH"] = (
            f"{os.environ.get('GST_PLUGIN_PATH', '')}:{plugins_dir}"
        )

    Gst.init(None)

    reg = Gst.Registry.get()
    if not reg.find_plugin("python"):
        raise RuntimeError(
            "GStreamer 'python' plugin not found. "
            "Ensure GST_PLUGIN_PATH includes the path to libgstpython.so. "
            "If the error persists, delete the GStreamer registry cache: "
            "rm ~/.cache/gstreamer-1.0/registry.x86_64.bin"
        )

def run_pipeline(pipeline_str: str) -> None:
    """Parse, run, and block on a GStreamer pipeline."""
    try:
        pipeline = Gst.parse_launch(pipeline_str)
        
    except GLib.Error as error:
        raise RuntimeError(f"Pipeline parse error: {error}") from error

    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)

    # Handle Ctrl-C: send EOS so muxers finalize properly
    def _sigint_handler(signum, frame):
        print("\n[pipeline] Ctrl-C received, sending EOS...")
        pipeline.send_event(Gst.Event.new_eos())

    prev_handler = signal.signal(signal.SIGINT, _sigint_handler)

    try:
        while True:
            message = bus.timed_pop_filtered(
                100 * Gst.MSECOND,
                Gst.MessageType.ERROR | Gst.MessageType.EOS,
            )
            if message is None:
                continue            
            if message.type == Gst.MessageType.ERROR:
                err, debug = message.parse_error()
                raise RuntimeError(f"Pipeline error: {err.message}\nDebug: {debug}")
            if message.type == Gst.MessageType.EOS:
                print("[pipeline] EOS received, shutting down")
                break
    finally:
        signal.signal(signal.SIGINT, prev_handler)
        pipeline.set_state(Gst.State.NULL)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="DLStreamer Loss Prevention sample")

    parser.add_argument("--video-url", default=DEFAULT_VIDEO_URL,
                        help="URL to download a video from (used when --video-path is omitted)")

    parser.add_argument("--detect-model-id", default="yolo26s", help="Ultralytics model id")
    parser.add_argument("--detect-device", default="GPU", help="Device for YOLO detection")
    parser.add_argument("--threshold", type=float, default=0.5,
                        help="Detection confidence threshold")

    return parser.parse_args()

def main() -> int:
    args = parse_args()

    video_file = download_video(args.video_url)
    model_file = download_detection_model(args.detect_model_id)

    setup_gst_plugins()
    pipeline_str = construct_pipeline(
        video_file, model_file, args.detect_device, args.threshold)
    run_pipeline(pipeline_str)

    return 0

if __name__ == "__main__":
    sys.exit(main())
