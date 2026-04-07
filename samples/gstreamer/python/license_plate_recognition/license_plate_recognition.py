# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
License Plate Recognition sample.

Detects license plates using a YOLOv8 detector and reads plate text with
PaddleOCR, all running inside a single DLStreamer GStreamer pipeline.

Usage:
    python3 license_plate_recognition.py [OPTIONS]

Examples:
    python3 license_plate_recognition.py
    python3 license_plate_recognition.py --video-path /path/to/video.mp4
    python3 license_plate_recognition.py --device CPU --output json
    python3 license_plate_recognition.py --output display-and-json
"""

import argparse
import os
import sys
import subprocess
import urllib.request
import zipfile
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
# pylint: disable-next=no-name-in-module, wrong-import-position
from gi.repository import GLib, Gst, GstAnalytics


# ---------------------------------------------------------------------------
# Directories
# ---------------------------------------------------------------------------

SAMPLE_DIR = Path(__file__).resolve().parent
VIDEOS_DIR = SAMPLE_DIR / "videos"
MODELS_DIR = SAMPLE_DIR / "models"

DEFAULT_VIDEO_URL = (
    "https://github.com/open-edge-platform/edge-ai-resources/raw/main/videos/ParkingVideo.mp4"
)

LPR_ZIP_URL = (
    "https://github.com/open-edge-platform/edge-ai-resources/raw/main/models/license-plate-reader.zip"
)


# ---------------------------------------------------------------------------
# Asset helpers
# ---------------------------------------------------------------------------


def download_video(url: str) -> Path:
    """Download a video file if not already cached locally."""
    VIDEOS_DIR.mkdir(parents=True, exist_ok=True)
    filename = url.rstrip("/").split("/")[-1]
    local = VIDEOS_DIR / filename
    if not local.exists():
        print(f"\nDownloading default video from {url} ...\n")
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=120) as resp:
            local.write_bytes(resp.read())
    return local.resolve()


def download_lpr_models() -> tuple:
    """Download and extract the license-plate-reader model pack.

    Returns (detection_xml, ocr_xml) paths.
    """
    detection_xml = MODELS_DIR / "yolov8_license_plate_detector" / "FP32" / "yolov8_license_plate_detector.xml"
    ocr_xml = MODELS_DIR / "ch_PP-OCRv4_rec_infer" / "FP32" / "ch_PP-OCRv4_rec_infer.xml"

    if detection_xml.exists() and ocr_xml.exists():
        return detection_xml, ocr_xml

    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    zip_path = MODELS_DIR / "license-plate-reader.zip"

    if not zip_path.exists():
        print(f"\nDownloading LPR model pack from {LPR_ZIP_URL} ...\n")
        req = urllib.request.Request(LPR_ZIP_URL, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=120) as resp:
            zip_path.write_bytes(resp.read())

    print("Extracting models ...\n")
    with zipfile.ZipFile(zip_path, "r") as zf:
        zf.extractall(MODELS_DIR)

    # Rearrange into expected layout
    extracted = MODELS_DIR / "license-plate-reader" / "models"

    det_dir = MODELS_DIR / "yolov8_license_plate_detector" / "FP32"
    det_dir.mkdir(parents=True, exist_ok=True)
    src_det_bin = extracted / "yolov8n" / "yolov8n_retrained.bin"
    src_det_xml = extracted / "yolov8n" / "yolov8n_retrained.xml"
    if src_det_xml.exists():
        src_det_xml.rename(det_dir / "yolov8_license_plate_detector.xml")
        src_det_bin.rename(det_dir / "yolov8_license_plate_detector.bin")

    ocr_dir = MODELS_DIR / "ch_PP-OCRv4_rec_infer" / "FP32"
    ocr_dir.mkdir(parents=True, exist_ok=True)
    src_ocr_bin = extracted / "ch_PP-OCRv4_rec_infer" / "ch_PP-OCRv4_rec_infer.bin"
    src_ocr_xml = extracted / "ch_PP-OCRv4_rec_infer" / "ch_PP-OCRv4_rec_infer.xml"
    if src_ocr_xml.exists():
        src_ocr_xml.rename(ocr_dir / "ch_PP-OCRv4_rec_infer.xml")
        src_ocr_bin.rename(ocr_dir / "ch_PP-OCRv4_rec_infer.bin")

    # Clean up
    import shutil
    extracted_root = MODELS_DIR / "license-plate-reader"
    if extracted_root.exists():
        shutil.rmtree(extracted_root)
    zip_path.unlink(missing_ok=True)

    return detection_xml, ocr_xml


# ---------------------------------------------------------------------------
# Pad probe — print detected plates to stdout
# ---------------------------------------------------------------------------


def on_plate_detected(pad, info, _user_data):
    """Pad probe callback: print recognized license plate text per frame."""
    buffer = info.get_buffer()
    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
    if rmeta:
        plates = []
        for mtd in rmeta:
            if isinstance(mtd, GstAnalytics.ODMtd):
                label = GLib.quark_to_string(mtd.get_obj_type())
                if label:
                    plates.append(label)
        if plates:
            pts_sec = buffer.pts / Gst.SECOND if buffer.pts != Gst.CLOCK_TIME_NONE else 0
            print(f"[{pts_sec:7.2f}s] Plates: {', '.join(plates)}")
    return Gst.PadProbeReturn.OK


# ---------------------------------------------------------------------------
# Pipeline construction
# ---------------------------------------------------------------------------


def build_pipeline_string(
    input_path: str,
    detection_model: str,
    ocr_model: str,
    device: str,
    output_mode: str,
) -> str:
    """Construct a gst-launch-style pipeline string."""

    # Source element
    if input_path.startswith("/dev/video"):
        source = f"v4l2src device={input_path}"
    elif "://" in input_path:
        source = f"urisourcebin buffer-size=4096 uri={input_path}"
    else:
        source = f'filesrc location="{input_path}"'

    # Decode — let GStreamer auto-negotiate
    decode = "decodebin3"

    # Inference chain: detect → classify
    detect = (
        f'gvadetect model="{detection_model}" device={device} batch-size=1'
    )
    classify = (
        f'gvaclassify model="{ocr_model}" device={device} batch-size=1'
    )

    # Sink / output
    if output_mode == "display":
        sink = "gvawatermark name=watermark ! videoconvertscale ! gvafpscounter ! autovideosink"
    elif output_mode == "fps":
        sink = "gvafpscounter ! fakesink async=false"
    elif output_mode == "json":
        sink = (
            "gvametaconvert ! "
            "gvametapublish file-format=json-lines file-path=results/output.jsonl ! "
            "fakesink async=false"
        )
    elif output_mode == "display-and-json":
        sink = (
            "gvawatermark name=watermark ! "
            "gvametaconvert ! "
            "gvametapublish file-format=json-lines file-path=results/output.jsonl ! "
            "videoconvertscale ! gvafpscounter ! autovideosink sync=false"
        )
    elif output_mode == "file":
        sink = (
            "gvawatermark name=watermark ! gvafpscounter ! "
            "videoconvert ! vah264enc ! h264parse ! mp4mux ! "
            "filesink location=results/output_lpr.mp4"
        )
    else:
        raise ValueError(f"Unknown output mode: {output_mode}")

    pipeline_str = (
        f"{source} ! {decode} ! "
        f"{detect} ! queue ! "
        f"{classify} ! queue ! "
        f"{sink}"
    )
    return pipeline_str


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------


def parse_args():
    parser = argparse.ArgumentParser(
        description="License Plate Recognition — DLStreamer Python Sample"
    )
    parser.add_argument(
        "--video-path",
        help="Path to a local video file. If omitted a default video is downloaded.",
    )
    parser.add_argument(
        "--video-url",
        default=DEFAULT_VIDEO_URL,
        help="URL to download the input video (used when --video-path is not set).",
    )
    parser.add_argument(
        "--device",
        default="GPU",
        choices=["CPU", "GPU", "AUTO"],
        help="Inference device (default: GPU).",
    )
    parser.add_argument(
        "--output",
        default="display-and-json",
        choices=["display", "fps", "json", "display-and-json", "file"],
        help="Output mode (default: display-and-json).",
    )
    return parser.parse_args()


# ---------------------------------------------------------------------------
# Pipeline event loop
# ---------------------------------------------------------------------------


def pipeline_loop(pipeline):
    print("\nStarting Pipeline ...\n")
    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)
    terminate = False
    while not terminate:
        msg = bus.timed_pop_filtered(
            Gst.CLOCK_TIME_NONE,
            Gst.MessageType.EOS | Gst.MessageType.ERROR,
        )
        if msg:
            if msg.type == Gst.MessageType.ERROR:
                _, debug_info = msg.parse_error()
                print(f"Error from {msg.src.get_name()}: {debug_info}")
                terminate = True
            if msg.type == Gst.MessageType.EOS:
                print("Pipeline complete.")
                terminate = True
    pipeline.set_state(Gst.State.NULL)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    args = parse_args()

    # --- Resolve input video ---
    if args.video_path:
        input_video = args.video_path
        if not os.path.isfile(input_video):
            sys.stderr.write(f"Error: video file not found: {input_video}\n")
            sys.exit(1)
    else:
        input_video = str(download_video(args.video_url))

    # --- Fallback to CPU when no GPU is available ---
    device = args.device
    if device == "GPU" and not os.path.exists("/dev/dri/renderD128"):
        print("GPU not available, falling back to CPU.\n")
        device = "CPU"

    # --- Download models ---
    detection_model, ocr_model = download_lpr_models()

    # --- Ensure results directory ---
    results_dir = SAMPLE_DIR / "results"
    results_dir.mkdir(parents=True, exist_ok=True)

    # --- Build pipeline ---
    Gst.init(None)

    pipeline_str = build_pipeline_string(
        input_path=input_video,
        detection_model=str(detection_model),
        ocr_model=str(ocr_model),
        device=device,
        output_mode=args.output,
    )
    print(f"\nPipeline:\n{pipeline_str}\n")

    pipeline = Gst.parse_launch(pipeline_str)

    # Attach pad probe to watermark element (if present) for live plate printing
    watermark = pipeline.get_by_name("watermark")
    if watermark:
        watermark.get_static_pad("sink").add_probe(
            Gst.PadProbeType.BUFFER, on_plate_detected, None
        )

    # --- Run ---
    pipeline_loop(pipeline)


if __name__ == "__main__":
    main()
