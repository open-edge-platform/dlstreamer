# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
This sample demonstrates how to create a simple object detection pipeline in Python
using fully programmatic approach where individual elements are instantiated and linked manually.
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

    pipeline = Gst.Pipeline()
    if not pipeline:
        sys.stderr.write(" Unable to create Pipeline \n")

    print("Creating Pipeline Elements  \n ")
    # Source element for reading from the file
    source = Gst.ElementFactory.make("filesrc", "file-source")
    if not source:
        sys.stderr.write(" Unable to create Source \n")

    # Use vah264 hardware accelerated decode on GPU
    decoder = Gst.ElementFactory.make("decodebin3", "media-decoder")
    if not decoder:
        sys.stderr.write(" Unable to create Media Decoder \n")

    # gvadetect to detect objects in video frames
    detect = Gst.ElementFactory.make("gvadetect", "object detector")
    if not detect:
        sys.stderr.write(" Unable to create gvadetect element \n")

    # Queue element decopules AI inference from rest of the pipeline
    queue = Gst.ElementFactory.make("queue", "queue")
    if not queue:
        sys.stderr.write(" Unable to create queue element \n")

    # Create gvawatermark to overlay prediction results on video frames
    watermark = Gst.ElementFactory.make("gvawatermark", "watermark")
    if not watermark:
        sys.stderr.write(" Unable to create gvawatermark element \n")

    videoconvertscale = Gst.ElementFactory.make("videoconvertscale", "videoconvertscale")
    if not videoconvertscale:
        sys.stderr.write(" Unable to create videoconvertscale element \n")

    # Create audio-video sink element to display decoding results
    sink = Gst.ElementFactory.make("autovideosink", "sink")
    if not sink:
        sys.stderr.write(" Unable to create sink element \n")

    # Configure elements
    print("Playing file %s " %args[1])
    source.set_property('location', args[1])
    detect.set_property('batch-size', 1)
    detect.set_property('device', "GPU")
    detect.set_property('model', args[2])

    print("Adding elements to Pipeline \n")
    pipeline.add(source)
    pipeline.add(decoder)
    pipeline.add(detect)
    pipeline.add(queue)
    pipeline.add(watermark)
    pipeline.add(videoconvertscale)
    pipeline.add(sink)

    # Link elements, late binding for decode->detect
    print("Linking elements in the Pipeline \n")
    source.link(decoder)
    decoder.connect("pad-added",
                    lambda element, pad, data: element.link(data) if pad.get_name().find("video") != -1 and not pad.is_linked() else None, 
                    detect)
    detect.link(queue)
    queue.link(watermark)
    watermark.link(videoconvertscale)
    videoconvertscale.link(sink)

    # STEP 2 - Add custom probe to the sink pad of the gvawatermark element.
    watermarksinkpad = watermark.get_static_pad("sink")
    if not watermarksinkpad:
        sys.stderr.write(" Unable to get sink pad of gvawatermark \n")
    watermarksinkpad.add_probe(Gst.PadProbeType.BUFFER, watermark_sink_pad_buffer_probe, 0)    

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
