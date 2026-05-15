# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
DL Streamer equivalent of DeepStream deepstream-test4.

Demonstrates how to:
- Use gvametaconvert and gvametapublish to publish detection metadata
  to file, Kafka, or MQTT (equivalent of DeepStream nvmsgconv + nvmsgbroker)
- Attach a pad probe to count detected objects per class
- Support multiple message broker backends via command-line arguments

Pipeline:
    filesrc → decodebin3 →
    gvadetect (YOLO11n) → queue →
    gvafpscounter → gvawatermark →
    gvametaconvert → gvametapublish →
    autovideosink / fakesink

Key difference from DeepStream test4:
    DL Streamer's gvametapublish is a pass-through transform (not a sink),
    so no tee is needed to split between message publishing and display.
    The pipeline is linear: detect → watermark → convert → publish → display.
"""

import argparse
import os
import signal
import sys
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")

from gi.repository import GLib, Gst, GstAnalytics  # noqa: E402

SCRIPT_DIR = Path(__file__).resolve().parent
MODELS_DIR = SCRIPT_DIR / "models"
RESULTS_DIR = SCRIPT_DIR / "results"

# COCO class IDs for the classes tracked by DeepStream test4
# YOLO11n uses COCO 80-class labels
COCO_CLASSES = {
    "car": 0, "truck": 1, "bus": 2,          # → DeepStream "Vehicle"
    "motorcycle": 3, "bicycle": 4,             # → DeepStream "TwoWheeler" / "Bicycle"
    "person": 5,                               # → DeepStream "Person"
}


# ── Pad probe: per-frame object counting ─────────────────────────────────────

def watermark_sink_pad_probe(pad, info, user_data):
    """Count detected objects per class and print summary.

    Equivalent to DeepStream's osd_sink_pad_buffer_probe.
    Key differences:
    - DL Streamer uses GstAnalytics metadata (not pyds batch/frame meta)
    - Probes run per-frame (not per-batch)
    - Message publishing is handled by gvametaconvert + gvametapublish
      elements in the pipeline (no manual NVDS_EVENT_MSG_META attachment)
    """
    buffer = info.get_buffer()
    if not buffer:
        return Gst.PadProbeReturn.OK

    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
    if not rmeta:
        return Gst.PadProbeReturn.OK

    frame_number = user_data.get("frame_count", 0)
    user_data["frame_count"] = frame_number + 1

    obj_counter = {"Vehicle": 0, "Person": 0, "TwoWheeler": 0}

    for mtd in rmeta:
        if isinstance(mtd, GstAnalytics.ODMtd):
            label = GLib.quark_to_string(mtd.get_obj_type())
            if label in ("car", "truck", "bus"):
                obj_counter["Vehicle"] += 1
            elif label == "person":
                obj_counter["Person"] += 1
            elif label in ("motorcycle", "bicycle"):
                obj_counter["TwoWheeler"] += 1

    # Print summary every 30 frames (matches DeepStream test4 frequency)
    if frame_number % 30 == 0:
        print(
            f"Frame Number = {frame_number} "
            f"Vehicle Count = {obj_counter['Vehicle']} "
            f"Person Count = {obj_counter['Person']} "
            f"TwoWheeler Count = {obj_counter['TwoWheeler']}"
        )

    return Gst.PadProbeReturn.OK


# ── Helpers ──────────────────────────────────────────────────────────────────

def parse_args():
    """Parse CLI arguments (mirrors DeepStream test4 options)."""
    parser = argparse.ArgumentParser(
        description="DL Streamer equivalent of DeepStream deepstream-test4"
    )
    parser.add_argument(
        "-i", "--input", required=True,
        help="Input video file path or rtsp:// URI",
    )
    parser.add_argument(
        "-m", "--method", default="file",
        choices=["file", "kafka", "mqtt"],
        help="Message publish method (default: file)",
    )
    parser.add_argument(
        "--address",
        help="Broker address (host:port for kafka/mqtt, file path for file method). "
             "Defaults: kafka=localhost:9092, mqtt=localhost:1883, file=results/results.jsonl",
    )
    parser.add_argument(
        "-t", "--topic", default="dlstreamer",
        help="Message topic for kafka/mqtt (default: dlstreamer)",
    )
    parser.add_argument(
        "-s", "--schema-type", type=int, default=0, choices=[0, 1],
        help="Message schema: 0=json (pretty), 1=json-lines (compact). Default: 0",
    )
    parser.add_argument(
        "--no-display", action="store_true",
        help="Disable video display (use fakesink)",
    )
    parser.add_argument(
        "--device", default="GPU",
        help="Inference device (default: GPU)",
    )
    return parser.parse_args()


def validate_input(source: str) -> str:
    """Validate video input path or RTSP URI."""
    if source.startswith("rtsp://"):
        return source
    if not os.path.isfile(source):
        sys.stderr.write(f"Error: file not found: {source}\n")
        sys.exit(1)
    return os.path.abspath(source)


def find_model() -> str:
    """Find the YOLO detection model .xml file."""
    hits = sorted(MODELS_DIR.glob("**/yolo*.xml"))
    if not hits:
        sys.stderr.write("Error: detection model not found. Run: python3 export_models.py\n")
        sys.exit(1)
    return str(hits[0])


def check_device(requested: str) -> str:
    """Check device availability with fallback chain: GPU → CPU."""
    if requested == "GPU" and not os.path.exists("/dev/dri/renderD128"):
        print("Warning: GPU not available, falling back to CPU")
        return "CPU"
    return requested


def build_source(src: str) -> str:
    """Build GStreamer source element string."""
    if src.startswith("rtsp://"):
        return f"rtspsrc location={src} latency=100"
    return f'filesrc location="{src}"'


def build_metapublish(args) -> str:
    """Build gvametaconvert + gvametapublish element string."""
    # Determine file format
    if args.schema_type == 0:
        file_format = "json"
        json_indent = 4
    else:
        file_format = "json-lines"
        json_indent = -1

    metaconvert = f"gvametaconvert json-indent={json_indent}"

    if args.method == "file":
        address = args.address or str(RESULTS_DIR / "results.jsonl")
        Path(address).parent.mkdir(parents=True, exist_ok=True)
        metapublish = (
            f'gvametapublish method=file file-format={file_format} '
            f'file-path="{address}"'
        )
    elif args.method == "kafka":
        address = args.address or "localhost:9092"
        metapublish = (
            f"gvametapublish method=kafka file-format=json-lines "
            f"address={address} topic={args.topic}"
        )
    elif args.method == "mqtt":
        address = args.address or "localhost:1883"
        metapublish = (
            f"gvametapublish method=mqtt file-format=json-lines "
            f"address={address} topic={args.topic}"
        )
    else:
        raise ValueError(f"Unknown method: {args.method}")

    return f"{metaconvert} ! {metapublish}"


def build_sink(no_display: bool) -> str:
    """Build the display/output sink element string."""
    if no_display:
        return "fakesink sync=false"
    return "autovideosink sync=false"


def run_pipeline(pipeline):
    """Event loop with SIGINT → EOS for graceful shutdown."""

    def _sigint(signum, frame):
        pipeline.send_event(Gst.Event.new_eos())

    prev = signal.signal(signal.SIGINT, _sigint)
    bus = pipeline.get_bus()

    print("[pipeline] Starting — first run may take time for model compilation...")
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


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    args = parse_args()

    # Validate input
    input_src = validate_input(args.input)

    # Locate model
    model_xml = find_model()

    # Device fallback
    device = check_device(args.device)

    # Init GStreamer
    Gst.init(None)

    # Build pipeline string
    source_el = build_source(input_src)
    meta_el = build_metapublish(args)
    sink_el = build_sink(args.no_display)

    pipe_str = (
        f'{source_el} ! decodebin3 caps="video/x-raw(ANY)" ! '
        f'gvadetect model="{model_xml}" device={device} batch-size=4 ! queue ! '
        f"gvafpscounter ! gvawatermark name=watermark ! "
        f"{meta_el} ! "
        f"videoconvert ! {sink_el}"
    )

    print(f"\nPipeline:\n{pipe_str}\n")

    pipeline = Gst.parse_launch(pipe_str)

    # Attach pad probe for object counting (equivalent to osd_sink_pad_buffer_probe)
    watermark = pipeline.get_by_name("watermark")
    watermark_sink_pad = watermark.get_static_pad("sink")
    probe_data = {"frame_count": 0}
    watermark_sink_pad.add_probe(
        Gst.PadProbeType.BUFFER, watermark_sink_pad_probe, probe_data
    )

    # Run
    run_pipeline(pipeline)

    if args.method == "file":
        address = args.address or str(RESULTS_DIR / "results.jsonl")
        print(f"\nOutput JSON: {address}")


if __name__ == "__main__":
    main()
