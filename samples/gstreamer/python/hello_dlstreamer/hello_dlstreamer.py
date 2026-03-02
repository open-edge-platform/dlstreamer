# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
This sample demonstrates how to create a simple object detection pipeline in Python
using Gst.parse_launch() method which resembles the gst-launch command line syntax.
"""

import sys
import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import GLib, Gst, GstAnalytics # pylint: disable=no-name-in-module, wrong-import-position

def watermark_sink_pad_buffer_probe(pad, info, _u_data):
    """Inspect detection result and accumulate object count for each category."""
    obj_counter = {}
    buffer = info.get_buffer()
    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)

    if rmeta:
        for mtd in rmeta:
            if isinstance(mtd, GstAnalytics.ODMtd):
                category = GLib.quark_to_string(mtd.get_obj_type())
                obj_counter[category] = obj_counter.get(category, 0) + 1
        rmeta.add_od_mtd(GLib.quark_from_string(f"Objects detected: {obj_counter}"),
                         10, 30, 0, 0, 0)

    return Gst.PadProbeReturn.OK

def main(args):
    # STEP 0 - Initialize GStreamer and check input arguments.
    Gst.init(None)
    if len(args) != 3:
        sys.stderr.write("usage: %s <LOCAL_VIDEO_FILE> <LOCAL_MODEL_FILE>\n" % args[0])
        sys.exit(1)

    # STEP 1 - Create GStreamer Pipeline.
    print("Creating Pipeline.\n")
    pipeline = Gst.parse_launch(
        f"filesrc location={args[1]} ! decodebin3 ! "
        f"gvadetect model={args[2]} device=GPU batch-size=1 ! queue ! "
        f"gvawatermark name=watermark ! videoconvertscale ! autovideosink"
    )

    # STEP 2 - Add custom probe to the sink pad of the gvawatermark element.
    pipeline.get_by_name("watermark").get_static_pad("sink").add_probe(
        Gst.PadProbeType.BUFFER, watermark_sink_pad_buffer_probe, 0)

    # STEP 3 - Eexecute pipeline.
    print("Starting Pipeline \n")
    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)
    terminate = False
    while not terminate:
        msg = bus.timed_pop_filtered(Gst.CLOCK_TIME_NONE, Gst.MessageType.EOS | Gst.MessageType.ERROR)
        if msg:
            if msg.type == Gst.MessageType.ERROR:
                _, debug_info = msg.parse_error()
                print(f"Error received from element {msg.src.get_name()}")
                print(f"Debug info: {debug_info}")
                terminate = True                
            if msg.type == Gst.MessageType.EOS:
                print(f"Pipeline complete.")
                terminate = True
    pipeline.set_state(Gst.State.NULL)

if __name__ == '__main__':
    sys.exit(main(sys.argv))