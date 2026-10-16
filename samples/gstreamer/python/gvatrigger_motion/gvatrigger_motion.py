# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
This sample demonstrates gvatrigger, a coded-size motion GOP gate for H.264/H.265.

gvatrigger reads only NAL unit sizes (no decoding, no pixels) and buffers access
units from the start of each GOP. If motion is detected, the buffer is flushed
and the rest of the GOP is forwarded live (sticky until the next IDR). If no
motion is detected before the next IDR, the entire buffer is discarded.

This sample tracks frame statistics to demonstrate bandwidth reduction: frames
are counted as either "dropped" (discarded quiet GOPs) or "forwarded" (motion
GOPs and live post-motion frames).

By default a longer synthetic 60 s scene is generated:
  static (15 s) -> moving ball (15 s) -> static (15 s) -> noise (15 s)

Pipeline (synthetic):
  videotestsrc -> <encoder> -> <parser> -> gvatrigger -> decodebin3 -> statistics -> fakesink

Pipeline (--input):
  urisourcebin -> parsebin -> gvatrigger -> decodebin3 -> statistics -> fakesink
"""

import argparse
import os
import sys

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
from gi.repository import Gst, GstBase, GObject  # pylint: disable=no-name-in-module, wrong-import-position

Gst.init(None)

FPS = 30
META_NAME = "GvaTriggerMeta"

# (name, videotestsrc pattern, duration in frames)
# Longer scene: 60 seconds total
SCENE = [
    ("static", "smpte75", 450),   # 15 s
    ("ball", "ball", 450),        # 15 s
    ("static", "smpte75", 450),   # 15 s
    ("noise", "snow", 450),       # 15 s
]

# encoder -> (raw format, encoder + parser); the format is pinned so VA encoders don't pick a 10/12-bit profile
ENCODERS = {
    "openh264": ("I420", "openh264enc gop-size=60 ! h264parse"),
    "vah264": ("NV12", "vah264enc key-int-max=60 rate-control=cqp ! h264parse"),
    "vah265": ("NV12", "vah265enc key-int-max=60 rate-control=cqp ! h265parse"),
}


class FrameCounter(GstBase.BaseTransform):
    """Counts frames passing through and tracks access unit statistics."""

    __gstmetadata__ = (
        "FrameCounter",
        "Filter/Video",
        "Counts frames after gvatrigger GOP gate for bandwidth reduction statistics",
        "Intel Corporation",
    )

    __gsttemplates__ = (
        Gst.PadTemplate.new("sink", Gst.PadDirection.SINK, Gst.PadPresence.ALWAYS, Gst.Caps.new_any()),
        Gst.PadTemplate.new("src", Gst.PadDirection.SRC, Gst.PadPresence.ALWAYS, Gst.Caps.new_any()),
    )

    def __init__(self):
        super().__init__()
        self.set_in_place(True)
        self.set_passthrough(True)
        self.frame_count = 0
        self.byte_count = 0

    def do_transform_ip(self, buffer):
        self.frame_count += 1
        self.byte_count += buffer.get_size()
        return Gst.FlowReturn.OK

    def get_stats(self):
        return {"frames": self.frame_count, "bytes": self.byte_count}


GObject.type_register(FrameCounter)


def scene_phases():
    """Return the scene as (name, pattern, first_frame, end_frame) tuples."""
    phases, start = [], 0
    for name, pattern, length in SCENE:
        phases.append((name, pattern, start, start + length))
        start += length
    return phases


def build_pipeline(args):
    """Build pipeline with gvatrigger GOP gate."""
    trigger = f"gvatrigger name=trigger k={args.k} alpha={args.alpha} margin={args.margin} max-cache-time-ms={args.max_cache_time_ms} min-baseline={args.min_baseline}"
    tail = f"{trigger} ! decodebin3 ! frame_counter name=counter ! "
    
    if args.output:
        tail += f"videoconvert ! video/x-raw,format=I420 ! openh264enc ! h264parse ! mp4mux ! filesink sync=false location=\"{os.path.abspath(args.output)}\""
    else:
        tail += "fakesink sync=false"

    if args.input:
        uri = args.input if "://" in args.input else Gst.filename_to_uri(os.path.abspath(args.input))
        return f"urisourcebin uri={uri} ! parsebin ! {tail}"

    total = sum(length for _, _, length in SCENE)
    raw_format, encoder = ENCODERS[args.encoder]
    return (
        f"videotestsrc name=src num-buffers={total} pattern={SCENE[0][1]} ! "
        f"video/x-raw,format={raw_format},width=640,height=480,framerate={FPS}/1 ! "
        f"{encoder} ! {tail}"
    )


def switch_patterns(src, phases):
    """Change the videotestsrc pattern at each scene boundary."""

    def on_buffer(_pad, info):
        nxt = int(round(info.get_buffer().pts * FPS / Gst.SECOND)) + 1
        for _, pattern, start, end in phases:
            if start <= nxt < end:
                Gst.util_set_object_arg(src, "pattern", pattern)
                break
        return Gst.PadProbeReturn.OK

    src.get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, on_buffer)


def print_summary(counter_element, input_frames):
    stats = counter_element.get_stats()
    forwarded = stats["frames"]
    dropped = input_frames - forwarded
    reduction_pct = 100.0 * dropped / max(input_frames, 1)

    print("\n=== gvatrigger GOP gate summary ===")
    print(f"input frames        : {input_frames}")
    print(f"forwarded frames    : {forwarded} ({100.0 * forwarded / max(input_frames, 1):.1f}%)")
    print(f"dropped frames      : {dropped} ({reduction_pct:.1f}%)")
    print(f"bandwidth reduction : {reduction_pct:.1f}%")
    print(f"data forwarded (KB) : {stats['bytes'] / 1024:.1f}")


def run_pipeline(pipeline_str, args):
    """Build, run pipeline and collect statistics."""
    pipeline = Gst.parse_launch(pipeline_str)
    if not pipeline:
        print(f"ERROR: Failed to parse pipeline: {pipeline_str}", file=sys.stderr)
        return False

    src = pipeline.get_by_name("src")
    counter = pipeline.get_by_name("counter")
    if src and counter:
        phases = scene_phases()
        switch_patterns(src, phases)

    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)

    try:
        while True:
            message = bus.timed_pop_filtered(Gst.CLOCK_TIME_NONE, Gst.MessageType.ANY)
            if not message:
                break
            if message.type == Gst.MessageType.EOS:
                print("EOS received")
                break
            elif message.type == Gst.MessageType.ERROR:
                gerror, debug = message.parse_error()
                print(f"ERROR: {gerror.message}", file=sys.stderr)
                if debug:
                    print(debug, file=sys.stderr)
                return False
    finally:
        pipeline.set_state(Gst.State.NULL)

    # Print summary
    if counter:
        total_frames = sum(length for _, _, length in SCENE)
        print_summary(counter, total_frames)

    return True


def main():
    parser = argparse.ArgumentParser(description="gvatrigger GOP gate bandwidth reduction sample")
    parser.add_argument("--input", help="Input video file or RTSP stream (default: synthetic scene)")
    parser.add_argument("--output", help="Output MP4 file (default: no file output)")
    parser.add_argument("--encoder", default="openh264", choices=ENCODERS.keys(),
                        help="Encoder for synthetic scene (default: openh264)")
    parser.add_argument("--k", type=float, default=3.0,
                        help="Motion threshold multiplier (default: 3.0)")
    parser.add_argument("--alpha", type=float, default=0.03,
                        help="EMA update rate for baseline (default: 0.03)")
    parser.add_argument("--margin", type=int, default=64,
                        help="Threshold margin in bytes (default: 64)")
    parser.add_argument("--max-cache-time-ms", type=int, default=4000,
                        help="Max cache time in milliseconds (default: 4000)")
    parser.add_argument("--min-baseline", type=int, default=100,
                        help="Minimum baseline in bytes (default: 100)")
    parser.add_argument("--gst-debug", help="GStreamer debug level (e.g., 'gvatrigger:6')")

    args = parser.parse_args()

    if args.gst_debug:
        Gst.debug_set_threshold_from_string(args.gst_debug, True)

    # Register custom FrameCounter element before building pipeline
    if not Gst.Element.register(None, "frame_counter", Gst.Rank.NONE, FrameCounter):
        print("ERROR: Failed to register frame_counter element", file=sys.stderr)
        sys.exit(1)

    pipeline_str = build_pipeline(args)
    print(f"Pipeline: {pipeline_str}\n")
    print(f"Parameters: k={args.k}, alpha={args.alpha}, margin={args.margin}, "
          f"max-cache-time-ms={args.max_cache_time_ms}, min-baseline={args.min_baseline}")

    success = run_pipeline(pipeline_str, args)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
