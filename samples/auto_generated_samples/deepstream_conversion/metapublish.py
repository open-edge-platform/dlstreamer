# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
DL Streamer Metadata Publishing sample — equivalent of DeepStream test4.

Demonstrates how to:
* Detect vehicles and persons using gvadetect (YOLO11n)
* Track objects across frames using gvatrack
* Convert inference metadata to JSON using gvametaconvert
* Publish metadata to file, MQTT, or Kafka using gvametapublish
* Count detected objects per frame using a pad probe callback

Pipeline:
    filesrc → decodebin3 →
    gvadetect → queue → gvatrack →
    gvafpscounter → gvawatermark →
    gvametaconvert → gvametapublish →
    videoconvert → vah264enc → h264parse → mp4mux → filesink

Unlike DeepStream's nvmsgbroker (a sink requiring a tee to split the stream),
DL Streamer's gvametapublish is a pass-through transform that forwards buffers
downstream — no tee is needed for combined publish + video output.
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

DEFAULT_VIDEO = str(SCRIPT_DIR / "videos" / "person-bicycle-car-detection.mp4")

# Class IDs matching COCO dataset labels used by YOLO11n
PGIE_CLASSES = {
    "person": 0,
    "bicycle": 1,
    "car": 2,
    "motorcycle": 3,
    "bus": 5,
    "truck": 7,
}


# ── helpers ──────────────────────────────────────────────────────────────────


def parse_args():
    p = argparse.ArgumentParser(
        description="DL Streamer Metadata Publishing (DeepStream test4 equivalent)"
    )
    p.add_argument(
        "--input", "-i",
        default=DEFAULT_VIDEO,
        help="Video file path or rtsp:// URI",
    )
    p.add_argument("--device", default="GPU", help="Inference device (default: GPU)")
    p.add_argument("--output-video", default=str(RESULTS_DIR / "output.mp4"))
    p.add_argument("--output-json", default=str(RESULTS_DIR / "results.jsonl"))
    p.add_argument(
        "--method", "-m",
        default="file",
        choices=["file", "mqtt", "kafka"],
        help="Publish method: file (default), mqtt, or kafka",
    )
    p.add_argument(
        "--address",
        default=None,
        help="Broker address for mqtt/kafka (default: localhost:1883 for mqtt, localhost:9092 for kafka)",
    )
    p.add_argument(
        "--topic", "-t",
        default="dlstreamer",
        help="Message topic for mqtt/kafka (default: dlstreamer)",
    )
    p.add_argument(
        "--schema-type", "-s",
        type=int, default=0, choices=[0, 1],
        help="Message schema: 0=json (pretty), 1=json-lines (compact). Default: 0",
    )
    p.add_argument(
        "--no-display",
        action="store_true",
        help="Disable video display (use fakesink instead of autovideosink)",
    )
    p.add_argument("--threshold", type=float, default=0.5, help="Detection confidence threshold")
    return p.parse_args()


def validate_input(source: str) -> str:
    if source.startswith("rtsp://"):
        return source
    if not os.path.isfile(source):
        sys.stderr.write(f"Error: file not found: {source}\n")
        sys.exit(1)
    return os.path.abspath(source)


def find_model(pattern: str, label: str) -> str:
    hits = sorted(MODELS_DIR.glob(pattern))
    if not hits:
        sys.stderr.write(f"Error: {label} model not found. Run: python3 export_models.py\n")
        sys.exit(1)
    return str(hits[0])


def check_device(requested: str, label: str) -> str:
    if requested == "NPU" and not os.path.exists("/dev/accel/accel0"):
        print(f"Warning: NPU not available for {label}, falling back to GPU")
        requested = "GPU"
    if requested == "GPU" and not os.path.exists("/dev/dri/renderD128"):
        print(f"Warning: GPU not available for {label}, falling back to CPU")
        requested = "CPU"
    return requested


def build_source(src: str) -> str:
    if src.startswith("rtsp://"):
        return f"rtspsrc location={src} latency=100"
    return f'filesrc location="{src}"'


def build_publish_element(args) -> str:
    """Build gvametapublish element string based on publish method."""
    if args.method == "file":
        file_format = "json" if args.schema_type == 0 else "json-lines"
        return (
            f'gvametapublish method=file file-format={file_format} '
            f'file-path="{args.output_json}"'
        )
    elif args.method == "mqtt":
        address = args.address or "localhost:1883"
        file_format = "json-lines"
        return (
            f'gvametapublish method=mqtt file-format={file_format} '
            f'address={address} topic={args.topic}'
        )
    elif args.method == "kafka":
        address = args.address or "localhost:9092"
        file_format = "json-lines"
        return (
            f'gvametapublish method=kafka file-format={file_format} '
            f'address={address} topic={args.topic}'
        )


# ── pad probe — object counting ─────────────────────────────────────────────


def watermark_sink_pad_buffer_probe(pad, info, _u_data):
    """Count vehicles and persons per frame and overlay the summary."""
    obj_counter = {}
    buffer = info.get_buffer()
    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)

    frame_number = buffer.pts  # use PTS as frame identifier

    if rmeta:
        for mtd in rmeta:
            if isinstance(mtd, GstAnalytics.ODMtd):
                category = GLib.quark_to_string(mtd.get_obj_type())
                obj_counter[category] = obj_counter.get(category, 0) + 1

        # Add summary overlay text
        vehicle_count = sum(
            obj_counter.get(cls, 0) for cls in ["car", "truck", "bus", "motorcycle"]
        )
        person_count = obj_counter.get("person", 0)

        summary = f"Vehicles: {vehicle_count} Persons: {person_count}"
        rmeta.add_od_mtd(GLib.quark_from_string(summary), 10, 30, 0, 0, 0)

        # Print summary to stdout (like DeepStream test4)
        print(f"PTS={frame_number} Vehicle Count={vehicle_count} Person Count={person_count}")

    return Gst.PadProbeReturn.OK


# ── pipeline event loop ─────────────────────────────────────────────────────


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

    # Locate model
    model_xml = find_model("**/*.xml", "detection")

    # Output dirs
    Path(args.output_video).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output_json).parent.mkdir(parents=True, exist_ok=True)

    # Device fallback
    device = check_device(args.device, "inference")

    # Build pipeline
    Gst.init(None)
    source_el = build_source(input_src)

    json_indent = 4 if args.schema_type == 0 else -1
    publish_el = build_publish_element(args)

    if args.no_display:
        sink_el = "fakesink sync=false"
    else:
        sink_el = (
            f"videoconvert ! vah264enc ! h264parse ! "
            f'mp4mux fragment-duration=1000 ! filesink location="{args.output_video}"'
        )

    pipe = (
        f'{source_el} ! decodebin3 caps="video/x-raw(ANY)" ! '
        f'gvadetect model="{model_xml}" device={device} '
        f"batch-size=4 threshold={args.threshold} ! queue ! "
        f"gvatrack tracking-type=zero-term-imageless ! "
        f"gvafpscounter ! gvawatermark name=watermark ! "
        f"gvametaconvert json-indent={json_indent} ! "
        f"{publish_el} ! "
        f"{sink_el}"
    )

    print(f"\nPipeline:\n{pipe}\n")
    pipeline = Gst.parse_launch(pipe)

    # Attach pad probe for object counting (like DeepStream test4)
    pipeline.get_by_name("watermark").get_static_pad("sink").add_probe(
        Gst.PadProbeType.BUFFER, watermark_sink_pad_buffer_probe, 0
    )

    run_pipeline(pipeline)

    if not args.no_display:
        print(f"\nOutput video: {args.output_video}")
    if args.method == "file":
        print(f"Output JSON:  {args.output_json}")
    else:
        print(f"Messages published to {args.method}://{args.address or 'localhost'} topic={args.topic}")


if __name__ == "__main__":
    main()
