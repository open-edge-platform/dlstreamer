# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
DLStreamer application template.
Replace TODO comments with application-specific logic.
"""

import sys

# --- TODO: Add additional standard-library imports as needed ---

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import GLib, Gst, GstAnalytics  # pylint: disable=no-name-in-module, wrong-import-position


# ---------------------------------------------------------------------------
# Pipeline event loop
# ---------------------------------------------------------------------------
def pipeline_loop(pipeline):
    """Run the GStreamer pipeline until EOS or error."""
    print("Starting Pipeline\n")
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
                print(f"Error received from element {msg.src.get_name()}")
                print(f"Debug info: {debug_info}")
                terminate = True
            if msg.type == Gst.MessageType.EOS:
                print("Pipeline complete.")
                terminate = True
    pipeline.set_state(Gst.State.NULL)


# ---------------------------------------------------------------------------
# TODO: Pad probe / appsink callback (delete if not needed)
# ---------------------------------------------------------------------------
def probe_callback(pad, info, user_data):
    """Inspect per-frame metadata."""
    buffer = info.get_buffer()
    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)
    if rmeta:
        for mtd in rmeta:
            if isinstance(mtd, GstAnalytics.ODMtd):
                label = GLib.quark_to_string(mtd.get_obj_type())
                # TODO: process detection result
    return Gst.PadProbeReturn.OK


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main(args):
    # TODO: Parse arguments (sys.argv for simple, argparse for complex)
    if len(args) < 2:
        sys.stderr.write(f"usage: {args[0]} <VIDEO_FILE> [MODEL_FILE]\n")
        sys.exit(1)

    video_file = args[1]
    # TODO: model_file = args[2]  or download/export model

    # Initialize GStreamer
    Gst.init(None)

    # TODO: Construct pipeline string
    pipeline_str = (
        f"filesrc location={video_file} ! decodebin3 ! "
        # f"gvadetect model={model_file} device=GPU batch-size=1 ! queue ! "
        f"gvawatermark name=watermark ! videoconvertscale ! autovideosink"
    )
    pipeline = Gst.parse_launch(pipeline_str)

    # TODO: Attach probes (delete if not needed)
    pipeline.get_by_name("watermark").get_static_pad("sink").add_probe(
        Gst.PadProbeType.BUFFER, probe_callback, None)

    # Execute pipeline
    pipeline_loop(pipeline)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
