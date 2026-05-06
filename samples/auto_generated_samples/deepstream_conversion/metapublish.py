# ==============================================================================
# Copyright (C) 2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
DL Streamer Metadata Publishing pipeline.

Equivalent of NVIDIA DeepStream deepstream-test4 sample.

Pipeline:
    filesrc / urisourcebin → decodebin3 →
    gvadetect (person-vehicle-bike-detection) → queue →
    gvatrack (object tracking) → queue →
    gvawatermark (overlay) →
    gvametaconvert (metadata → JSON) →
    gvametapublish (publish to file / Kafka / MQTT) →
    autovideosink / fakesink

Supports publishing inference metadata to:
  - File (stdout or file path)
  - Kafka message broker
  - MQTT message broker
"""

import argparse
import os
import signal
import sys

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")

from gi.repository import GLib, Gst, GstAnalytics  # noqa: E402


# Class IDs for person-vehicle-bike-detection-2004
CLASS_VEHICLE = "vehicle"
CLASS_PERSON = "person"
CLASS_BIKE = "bike"


def parse_args():
    parser = argparse.ArgumentParser(
        description="DL Streamer Metadata Publishing Sample "
        "(equivalent of DeepStream deepstream-test4)"
    )
    parser.add_argument(
        "-i",
        "--input",
        required=True,
        help="Path to input video file or URI",
    )
    parser.add_argument(
        "-p",
        "--method",
        default="file",
        choices=["file", "kafka", "mqtt"],
        help="Publishing method: file, kafka, or mqtt (default: file)",
    )
    parser.add_argument(
        "--conn-str",
        default=None,
        help="Connection string. For Kafka: host:port (default: localhost:9092). "
        "For MQTT: host:port (default: localhost:1883). "
        "For file: output file path (default: stdout)",
    )
    parser.add_argument(
        "-t",
        "--topic",
        default="dlstreamer",
        help="Topic name for Kafka/MQTT (default: dlstreamer)",
    )
    parser.add_argument(
        "-s",
        "--schema-type",
        type=int,
        default=0,
        choices=[0, 1],
        help="JSON format: 0=pretty-print JSON, 1=JSON Lines (default: 0)",
    )
    parser.add_argument(
        "-c",
        "--cfg-file",
        default=None,
        help="Path to MQTT configuration file (JSON). Optional.",
    )
    parser.add_argument(
        "--no-display",
        action="store_true",
        default=False,
        help="Disable video display (use fakesink)",
    )
    parser.add_argument(
        "--device",
        default="CPU",
        help="Inference device: CPU, GPU, AUTO (default: CPU)",
    )
    parser.add_argument(
        "--model",
        default=None,
        help="Path to detection model .xml file. "
        "Default: $MODELS_PATH/intel/person-vehicle-bike-detection-2004/FP32/"
        "person-vehicle-bike-detection-2004.xml",
    )
    parser.add_argument(
        "--model-proc",
        default=None,
        help="Path to model-proc .json file. Default: auto-detected.",
    )
    parser.add_argument(
        "--tracking-type",
        default="short-term-imageless",
        help="Object tracking type (default: short-term-imageless)",
    )
    parser.add_argument(
        "--detection-interval",
        type=int,
        default=1,
        help="Run detection every Nth frame (default: 1, every frame)",
    )
    return parser.parse_args()


def find_model(model_arg):
    """Find the detection model XML file."""
    if model_arg:
        if not os.path.isfile(model_arg):
            sys.stderr.write(f"Error: model file not found: {model_arg}\n")
            sys.exit(1)
        return model_arg

    models_path = os.environ.get("MODELS_PATH", "")
    if not models_path:
        sys.stderr.write(
            "Error: --model not specified and MODELS_PATH env variable not set.\n"
            "Please set MODELS_PATH or provide --model argument.\n"
            "Download models with: ./samples/download_omz_models.sh\n"
        )
        sys.exit(1)

    model_name = "person-vehicle-bike-detection-2004"
    model_path = os.path.join(
        models_path, "intel", model_name, "FP32", f"{model_name}.xml"
    )
    if not os.path.isfile(model_path):
        sys.stderr.write(f"Error: model not found at {model_path}\n")
        sys.exit(1)
    return model_path


def find_model_proc(model_proc_arg):
    """Find the model-proc JSON file."""
    if model_proc_arg:
        if not os.path.isfile(model_proc_arg):
            sys.stderr.write(f"Error: model-proc file not found: {model_proc_arg}\n")
            sys.exit(1)
        return model_proc_arg

    # Check relative to this script's location (samples directory structure)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(
            script_dir,
            "..",
            "gst_launch",
            "vehicle_pedestrian_tracking",
            "model_proc",
            "person-vehicle-bike-detection-2004.json",
        ),
    ]
    for candidate in candidates:
        if os.path.isfile(candidate):
            return os.path.abspath(candidate)

    # model-proc is optional for this model; return None
    return None


def build_source(input_path):
    """Build GStreamer source element string."""
    if input_path.startswith("rtsp://"):
        return f"rtspsrc location={input_path} latency=100"
    if "://" in input_path:
        return f"urisourcebin buffer-size=4096 uri={input_path}"
    if not os.path.isfile(input_path):
        sys.stderr.write(f"Error: input file not found: {input_path}\n")
        sys.exit(1)
    return f'filesrc location="{os.path.abspath(input_path)}"'


def build_publish_elements(args):
    """Build gvametaconvert + gvametapublish element string."""
    # JSON indent: pretty-print (4 spaces) for schema_type=0, compact (-1) for schema_type=1
    json_indent = 4 if args.schema_type == 0 else -1
    file_format = "json" if args.schema_type == 0 else "json-lines"

    metaconvert = f"gvametaconvert json-indent={json_indent}"

    # Build gvametapublish properties
    publish_props = f"method={args.method} file-format={file_format}"

    if args.method == "file":
        if args.conn_str:
            publish_props += f' file-path="{args.conn_str}"'
        # else defaults to stdout
    elif args.method in ("kafka", "mqtt"):
        default_addr = (
            "localhost:9092" if args.method == "kafka" else "localhost:1883"
        )
        address = args.conn_str if args.conn_str else default_addr
        publish_props += f" address={address} topic={args.topic}"
        if args.cfg_file and args.method == "mqtt":
            publish_props += f' mqtt-config="{args.cfg_file}"'

    metapublish = f"gvametapublish {publish_props}"

    return f"{metaconvert} ! {metapublish}"


def watermark_probe(pad, info, _user_data):
    """Pad probe callback on watermark sink pad.

    Counts detected objects per class and prints summary.
    Equivalent of osd_sink_pad_buffer_probe in DeepStream test4.
    """
    buffer = info.get_buffer()
    if not buffer:
        return Gst.PadProbeReturn.OK

    obj_counter = {}
    frame_number = buffer.pts

    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
    if rmeta:
        for mtd in rmeta:
            if isinstance(mtd, GstAnalytics.ODMtd):
                label = GLib.quark_to_string(mtd.get_obj_type())
                obj_counter[label] = obj_counter.get(label, 0) + 1

    vehicle_count = obj_counter.get(CLASS_VEHICLE, 0)
    person_count = obj_counter.get(CLASS_PERSON, 0)
    bike_count = obj_counter.get(CLASS_BIKE, 0)

    print(
        f"PTS={frame_number} "
        f"Vehicle Count={vehicle_count} "
        f"Person Count={person_count} "
        f"Bike Count={bike_count}"
    )

    return Gst.PadProbeReturn.OK


def run_pipeline(pipeline):
    """Run pipeline event loop with SIGINT handling."""

    def _sigint(signum, frame):
        pipeline.send_event(Gst.Event.new_eos())

    prev = signal.signal(signal.SIGINT, _sigint)
    bus = pipeline.get_bus()

    pipeline.set_state(Gst.State.PLAYING)
    print("Pipeline started.\n")

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
                print(
                    f"Error from {msg.src.get_name()}: {err.message}\nDebug: {dbg}",
                    file=sys.stderr,
                )
                break
            if msg.type == Gst.MessageType.EOS:
                print("\nPipeline complete.")
                break
    finally:
        signal.signal(signal.SIGINT, prev)
        pipeline.set_state(Gst.State.NULL)


def main():
    args = parse_args()

    # Initialize GStreamer
    Gst.init(None)

    # Find model
    model_xml = find_model(args.model)
    model_proc = find_model_proc(args.model_proc)

    # Build pipeline elements
    source_el = build_source(args.input)
    publish_el = build_publish_elements(args)

    # Detection element
    detect_props = f'model="{model_xml}" device={args.device}'
    if model_proc:
        detect_props += f' model-proc="{model_proc}"'
    if args.detection_interval > 1:
        detect_props += f" inference-interval={args.detection_interval}"

    # Sink element
    if args.no_display:
        sink_el = "fakesink sync=false"
    else:
        sink_el = "videoconvertscale ! autovideosink sync=false"

    # Assemble pipeline
    # Key difference from DeepStream test4:
    #   - gvametapublish is a TRANSFORM (not a sink), so no tee is needed
    #   - gvametaconvert + gvametapublish sit inline between watermark and sink
    pipe = (
        f"{source_el} ! decodebin3 ! "
        f"gvadetect {detect_props} ! queue ! "
        f"gvatrack tracking-type={args.tracking_type} ! queue ! "
        f"gvawatermark name=watermark ! "
        f"{publish_el} ! "
        f"{sink_el}"
    )

    print(f"Pipeline:\n  {pipe}\n")
    pipeline = Gst.parse_launch(pipe)

    # Add probe to watermark sink pad for object counting
    # (equivalent of osd_sink_pad_buffer_probe in DeepStream test4)
    watermark = pipeline.get_by_name("watermark")
    if watermark:
        watermark.get_static_pad("sink").add_probe(
            Gst.PadProbeType.BUFFER, watermark_probe, None
        )

    # Run pipeline
    run_pipeline(pipeline)


if __name__ == "__main__":
    main()
