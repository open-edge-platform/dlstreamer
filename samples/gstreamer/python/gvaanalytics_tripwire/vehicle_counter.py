# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
This sample demonstrates how to count vehicles crossing a tripwire using gvaanalytics.
The pipeline detects vehicles, tracks them across frames, and counts crossings in both directions.

A custom GstBaseTransform element displays the crossing counters as watermark text.

Pipeline:
  filesrc -> decodebin3 -> gvadetect -> gvatrack -> gvaanalytics ->
  vehicle_counter_text -> gvawatermark -> gvafpscounter ->
  videoconvert -> autovideosink
"""

import sys
import os
import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
gi.require_version("GstAnalytics", "1.0")
gi.require_version("DLStreamerMeta", "1.0")
gi.require_version("DLStreamerWatermarkMeta", "1.0")
from gi.repository import Gst, GstBase, GObject, GLib, GstAnalytics, DLStreamerMeta, DLStreamerWatermarkMeta  # pylint: disable=no-name-in-module, wrong-import-position

from gstgva.region_of_interest import RegionOfInterest 

Gst.init(None)
Gst.init_python()

# Global counter for vehicles crossing the horizontal tripwire
crossing_count = {
    "left_to_right": 0,
    "right_to_left": 0,
    "total": 0
}


# ---------------------------------------------------------------------------
# Custom GstBaseTransform element to count and display vehicle crossings
# ---------------------------------------------------------------------------

class VehicleCounterText(GstBase.BaseTransform):
    """Custom GStreamer element that counts tripwire crossings and adds watermark text."""

    __gstmetadata__ = (
        "VehicleCounterText",
        "Filter/Analytics",
        "Counts vehicle crossings and displays counters as watermark text",
        "Intel Corporation",
    )

    __gsttemplates__ = (
        Gst.PadTemplate.new("sink", Gst.PadDirection.SINK, Gst.PadPresence.ALWAYS,
                            Gst.Caps.new_any()),
        Gst.PadTemplate.new("src",  Gst.PadDirection.SRC,  Gst.PadPresence.ALWAYS,
                            Gst.Caps.new_any()),
    )

    # Property: vehicle types to track
    _vehicle_types = "car,bus,truck"

    @GObject.Property(type=str)
    def vehicle_types(self):
        """Comma-separated list of vehicle types to count (e.g., 'car,bus,truck')"""
        return self._vehicle_types

    @vehicle_types.setter
    def vehicle_types(self, value):
        self._vehicle_types = value
        # Update the allowed types set when property changes
        self._allowed_types = set(t.strip().lower() for t in value.split(","))

    def __init__(self):
        super().__init__()
        self.set_in_place(True)
        self.set_passthrough(False)
        self._allowed_types = set(t.strip().lower() for t in self._vehicle_types.split(","))

    def do_transform_ip(self, buffer):
        """Process buffer, count tripwire crossings, and add watermark text."""
        relation_meta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)

        if relation_meta:
            for mtd in relation_meta:
                if isinstance(mtd, DLStreamerMeta.TripwireMtd):
                    success, tripwire_id, direction = mtd.get_info()
                    if not success or direction == 0:
                        continue

                    # Walk all metadata to find the ODMtd related to this tripwire
                    crossing_obj_type = None
                    for related_mtd in relation_meta:
                        if not isinstance(related_mtd, GstAnalytics.ODMtd):
                            continue
                        rel = relation_meta.get_relation(related_mtd.id, mtd.id)
                        if rel & GstAnalytics.RelTypes.RELATE_TO:
                            crossing_obj_type = GLib.quark_to_string(related_mtd.get_obj_type())
                            break

                    # Now filter on the actual crossing object's type
                    if not crossing_obj_type and crossing_obj_type.lower() not in self._allowed_types:
                        continue  # Skip non-vehicle crossings

                    # Count the crossing
                    if direction == 1:  # Left-to-right crossing
                        crossing_count["left_to_right"] += 1
                        crossing_count["total"] += 1
                        print(f"Vehicle crossing (L→R) - {crossing_obj_type}! Total: {crossing_count['total']}")
                    elif direction == -1:  # Right-to-left crossing
                        crossing_count["right_to_left"] += 1
                        crossing_count["total"] += 1
                        print(f"Vehicle crossing (R→L) - {crossing_obj_type}! Total: {crossing_count['total']}")

        # Add single-line watermark text displaying all counters
        counter_text = (f"L to R: {crossing_count['left_to_right']}  |  "
                       f"R to L: {crossing_count['right_to_left']}  |  "
                       f"Total: {crossing_count['total']}")
        
        DLStreamerWatermarkMeta.text_meta_add(
            buffer,
            x=10, y=30,
            text=counter_text,
            font_scale=0.8,
            font_type=0,  # cv::FONT_HERSHEY_SIMPLEX
            r=0, g=255, b=255, thickness=1, draw_bg=True)

        return Gst.FlowReturn.OK


GObject.type_register(VehicleCounterText)
__gstelementfactory__ = ("vehicle_counter_text", Gst.Rank.NONE, VehicleCounterText)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(args):
    if len(args) < 3 or len(args) > 6:
        sys.stderr.write("usage: %s <VIDEO_FILE_OR_URL> <LOCAL_MODEL_FILE> [OUTPUT_VIDEO] [DEVICE] [JSON_METADATA_FILE]\n" % args[0])
        sys.stderr.write("\nParameters:\n")
        sys.stderr.write("  VIDEO_FILE_OR_URL: Local video file or HTTP(S) URL\n")
        sys.stderr.write("  LOCAL_MODEL_FILE: Path to detection model (XML file, e.g., yolo11n.xml)\n")
        sys.stderr.write("  OUTPUT_VIDEO: Optional output MP4 file (default: display on screen)\n")
        sys.stderr.write("  DEVICE: Inference device - GPU (default), CPU, or NPU\n")
        sys.stderr.write("  JSON_METADATA_FILE: Optional output file for analytics in JSON Lines format\n")
        sys.stderr.write("                      Each line contains: detections, tracks, and tripwire crossing events\n")
        sys.exit(1)

    video_file = args[1]
    model_file = args[2]
    output_file = args[3] if len(args) > 3 else None
    device = args[4] if len(args) > 4 else "GPU"
    json_metadata_file = args[5] if len(args) > 5 else None

    # Validate model file exists
    if not os.path.isfile(model_file):
        sys.stderr.write(f"Error: Model file not found: {model_file}\n")
        sys.exit(1)

    # Validate device parameter
    if device not in ["CPU", "GPU", "NPU"]:
        sys.stderr.write(f"Warning: Unknown device '{device}'. Supported: CPU, GPU, NPU. Using as-is.\n")

    # Register the custom element so it can be used in parse_launch
    if not Gst.Element.register(None, "vehicle_counter_text", Gst.Rank.NONE, VehicleCounterText):
        sys.stderr.write("Failed to register vehicle_counter_text element\n")
        sys.exit(1)

    # Get path to tripwire config file
    config_file = os.path.join(os.path.dirname(__file__), "tripwire-config.json")

    # Build output sink based on whether file output is requested
    if output_file:
        output_sink = (
            f"videoconvert ! "
            f"openh264enc ! "
            f"h264parse ! "
            f"mp4mux ! "
            f"filesink location={output_file}"
        )
        print(f"Output will be saved to: {output_file}\n")
    else:
        output_sink = "videoconvert ! autovideosink"
        print("Output will be displayed on screen (no file saving)\n")

    # Build optional metadata publishing pipeline segment (placed after gvawatermark)
    metadata_pipeline = ""
    if json_metadata_file:
        metadata_pipeline = f"! gvametaconvert ! gvametapublish file-format=json-lines file-path={json_metadata_file} "

    # Build pipeline using parse_launch
    pipeline_str = (
        f"urisourcebin uri={video_file} ! "
        f"decodebin3 ! "
        f"gvadetect model={model_file} device={device} threshold=0.7 ! queue ! "
        f"gvatrack tracking-type=zero-term ! queue ! "
        f"gvaanalytics config={config_file} ! "
        f"vehicle_counter_text vehicle-types=car,bus,truck ! "
        f"gvawatermark {metadata_pipeline}! "
        f"gvafpscounter ! "
        f"{output_sink}"
    )

    print(f"Creating pipeline...")
    print(f"  Model: {model_file}")
    print(f"  Device: {device}")
    if json_metadata_file:
        print(f"  Metadata output: {json_metadata_file}")
    print()

    pipeline = Gst.parse_launch(pipeline_str)

    if not pipeline:
        sys.stderr.write("Failed to create pipeline\n")
        sys.exit(1)

    print("Starting pipeline...\n")
    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)

    terminate = False
    if output_file:
        print(f"Output will be saved to: {output_file}\n")

    while not terminate:
        msg = bus.timed_pop_filtered(
            Gst.CLOCK_TIME_NONE,
            Gst.MessageType.EOS | Gst.MessageType.ERROR)
        if msg:
            if msg.type == Gst.MessageType.ERROR:
                err, debug_info = msg.parse_error()
                print(f"Error from element {msg.src.get_name()}: {err.message}")
                print(f"Debug info: {debug_info}")
                terminate = True
            elif msg.type == Gst.MessageType.EOS:
                print("Pipeline complete.")
                print(f"\n=== Final Vehicle Crossing Counts ===")
                print(f"Left to Right (L→R): {crossing_count['left_to_right']}")
                print(f"Right to Left (R→L): {crossing_count['right_to_left']}")
                print(f"Total Crossings: {crossing_count['total']}")
                terminate = True

    pipeline.set_state(Gst.State.NULL)

if __name__ == "__main__":
    sys.exit(main(sys.argv))
