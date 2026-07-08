# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
This sample demonstrates how to attach custom watermark metadata to a GStreamer
buffer using DLStreamer's watermark metadata types (draw, circle, text).

A custom GstBaseTransform element is used to add watermark metadata to buffers.

Pipeline:
  urisourcebin -> decodebin3 -> videoconvert -> watermark_meta_adder ->
  gvawatermark ->
  videoconvert -> openh264enc -> h264parse -> mp4mux -> filesink
"""

import os
import sys
import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
gi.require_version("DLStreamerWatermarkMeta", "1.0")
from gi.repository import Gst, GstBase, GObject, DLStreamerWatermarkMeta  # pylint: disable=no-name-in-module, wrong-import-position

Gst.init(None)
Gst.init_python()

# Number of frames produced by the deterministic videotestsrc source
NUM_BUFFERS = 10

# ---------------------------------------------------------------------------
# Custom GstBaseTransform element to add watermark metadata
# ---------------------------------------------------------------------------

class WatermarkMetaAdder(GstBase.BaseTransform):
    """Custom GStreamer element that adds DLStreamer watermark metadata to each buffer."""

    __gstmetadata__ = (
        "WatermarkMetaAdder",
        "Filter/Video",
        "Attaches DLStreamer watermark metadata to video buffers",
        "Intel Corporation",
    )

    __gsttemplates__ = (
        Gst.PadTemplate.new("sink", Gst.PadDirection.SINK, Gst.PadPresence.ALWAYS,
                            Gst.Caps.new_any()),
        Gst.PadTemplate.new("src",  Gst.PadDirection.SRC,  Gst.PadPresence.ALWAYS,
                            Gst.Caps.new_any()),
    )

    def __init__(self):
        super().__init__()
        self.set_in_place(True)
        self.set_passthrough(False)

    def do_transform_ip(self, buffer):

        # 1 - Polygon (6 points) in green  [x1,y1, x2,y2, ...]
        DLStreamerWatermarkMeta.draw_meta_add(
            buffer,
            [100, 50, 200, 50, 250, 150, 200, 250, 100, 250, 50, 150],
            r=0, g=200, b=0, thickness=3)

        # 2 - Line (2 points) in red
        DLStreamerWatermarkMeta.draw_meta_add(
            buffer,
            [300, 80, 500, 200],
            r=220, g=20, b=20, thickness=4)

        # 3 - Filled circle in blue  (thickness=-1 → filled)
        DLStreamerWatermarkMeta.circle_meta_add(
            buffer,
            cx=570, cy=150, radius=50,
            r=30, g=80, b=220, thickness=-1)

        # 4 - Text with background in yellow
        DLStreamerWatermarkMeta.text_meta_add(
            buffer,
            x=50, y=300,
            text="DLStreamer watermark meta",
            font_scale=0.8,
            font_type=4,  # cv::FONT_HERSHEY_TRIPLEX
            r=220, g=200, b=0, thickness=2, draw_bg=True)

        # 5 - Text without background in cyan
        DLStreamerWatermarkMeta.text_meta_add(
            buffer,
            x=50, y=335,
            text="No background text",
            font_scale=0.7,
            font_type=1,  # cv::FONT_HERSHEY_PLAIN
            r=0, g=200, b=200, thickness=2, draw_bg=False)

        return Gst.FlowReturn.OK


GObject.type_register(WatermarkMetaAdder)
__gstelementfactory__ = ("watermark_meta_adder", Gst.Rank.NONE, WatermarkMetaAdder)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def build_source(input_arg):
    """Return the source part of the pipeline.

    ``videotestsrc``/``test`` produces a deterministic white test pattern,
    otherwise the input is treated as a video file/URI.
    """
    if input_arg in ("videotestsrc", "test"):
        return (
            f"videotestsrc num-buffers={NUM_BUFFERS} pattern=white ! "
            f"video/x-raw,width=640,height=480,framerate=30/1 ! "
            f"videoconvert ! video/x-raw,format=BGR"
        )
    return (
        f"urisourcebin uri={input_arg} ! decodebin3 ! "
        f"videoconvert ! video/x-raw,format=BGR"
    )


def build_sink(output_arg):
    """Return the sink part of the pipeline.

    An ``.mp4`` output is encoded to a video file, any other value is treated
    as a directory and watermarked frames are written as PNG images.
    """
    if output_arg.endswith(".mp4"):
        return (
            f"videoconvert ! openh264enc ! h264parse ! mp4mux ! "
            f"filesink location={output_arg}"
        )
    os.makedirs(output_arg, exist_ok=True)
    location = os.path.join(output_arg, "frame_%05d.png")
    return f"videoconvert ! video/x-raw,format=RGB ! pngenc ! multifilesink location={location}"


def main(args):
    if len(args) != 3:
        sys.stderr.write(
            "usage: %s <INPUT_VIDEO_URI|videotestsrc> <OUTPUT_MP4_FILE|FRAMES_DIR>\n" % args[0])
        sys.exit(1)

    input_arg, output_arg = args[1], args[2]

    # Register the custom element so it can be used in parse_launch
    if not Gst.Element.register(None, "watermark_meta_adder", Gst.Rank.NONE, WatermarkMetaAdder):
        sys.stderr.write("Failed to register watermark_meta_adder element\n")
        sys.exit(1)

    pipeline = Gst.parse_launch(
        f"{build_source(input_arg)} ! "
        f"watermark_meta_adder ! "
        f"gvawatermark ! "
        f"gvafpscounter ! "
        f"{build_sink(output_arg)}"
    )

    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)

    terminate = False
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
                terminate = True

    pipeline.set_state(Gst.State.NULL)
    print(f"Output written to: {output_arg}")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
