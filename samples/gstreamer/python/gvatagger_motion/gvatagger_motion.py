# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""
This sample demonstrates gvatagger, a coded-size motion tagger for H.264/H.265.

gvatagger reads only NAL unit sizes (no decoding, no pixels) and attaches a
GvaTriggerMeta custom meta (motion, keyframe) to every access unit. The meta
survives decoding, so this sample reads it after the decoder and prints a
motion summary.

By default a synthetic scene is generated and encoded:
  static (5 s) -> moving ball (3 s) -> static (5 s) -> noise (2 s) -> static (5 s)

Pipeline (synthetic):
  videotestsrc -> <encoder> -> <parser> -> gvatagger -> decodebin3 -> gvafpscounter -> fakesink

Pipeline (--input):
  urisourcebin -> parsebin -> gvatagger -> decodebin3 -> gvafpscounter -> fakesink

With --output, fakesink is replaced by:
  videoconvert -> motion_overlay -> gvawatermark -> videoconvert -> openh264enc -> mp4mux -> filesink
where motion_overlay is a Python element that adds a red border and a "MOTION"
label (DL Streamer watermark meta) to frames tagged as motion.
"""

import argparse
import os
import sys

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
gi.require_version("DLStreamerWatermarkMeta", "1.0")
from gi.repository import Gst, GstBase, GObject, DLStreamerWatermarkMeta  # pylint: disable=no-name-in-module, wrong-import-position

Gst.init(None)

FPS = 30
META_NAME = "GvaTriggerMeta"

# (name, videotestsrc pattern, duration in frames)
SCENE = [
    ("static", "smpte75", 150),
    ("ball", "ball", 90),
    ("static", "smpte75", 150),
    ("noise", "snow", 60),
    ("static", "smpte75", 150),
]

# encoder -> (raw format, encoder + parser); the format is pinned so VA encoders don't pick a 10/12-bit profile
ENCODERS = {
    "openh264": ("I420", "openh264enc gop-size=60 ! h264parse"),
    "vah264": ("NV12", "vah264enc key-int-max=60 rate-control=cqp ! h264parse"),
    "vah265": ("NV12", "vah265enc key-int-max=60 rate-control=cqp ! h265parse"),
}


class MotionOverlay(GstBase.BaseTransform):
    """Adds a red border and a MOTION label to frames that gvatagger tagged as motion."""

    __gstmetadata__ = (
        "MotionOverlay",
        "Filter/Video",
        "Attaches DL Streamer watermark meta to frames tagged as motion by gvatagger",
        "Intel Corporation",
    )

    __gsttemplates__ = (
        Gst.PadTemplate.new("sink", Gst.PadDirection.SINK, Gst.PadPresence.ALWAYS, Gst.Caps.new_any()),
        Gst.PadTemplate.new("src", Gst.PadDirection.SRC, Gst.PadPresence.ALWAYS, Gst.Caps.new_any()),
    )

    def __init__(self):
        super().__init__()
        self.set_in_place(True)
        self.set_passthrough(False)
        self.width = 0
        self.height = 0

    def do_set_caps(self, incaps, outcaps):  # pylint: disable=unused-argument
        s = incaps.get_structure(0)
        self.width = s.get_value("width")
        self.height = s.get_value("height")
        return True

    def do_transform_ip(self, buffer):
        meta = buffer.get_custom_meta(META_NAME)
        if meta is None or not meta.get_structure().get_value("motion"):
            return Gst.FlowReturn.OK

        right, bottom = self.width - 1, self.height - 1
        DLStreamerWatermarkMeta.draw_meta_add(
            buffer, [0, 0, right, 0, right, bottom, 0, bottom, 0, 0], r=220, g=20, b=20, thickness=6)
        DLStreamerWatermarkMeta.text_meta_add(
            buffer, x=20, y=45, text="MOTION DETECTED", font_scale=1.2,
            font_type=0,  # cv::FONT_HERSHEY_SIMPLEX
            r=220, g=20, b=20, thickness=2, draw_bg=True)
        return Gst.FlowReturn.OK


GObject.type_register(MotionOverlay)


def scene_phases():
    """Return the scene as (name, pattern, first_frame, end_frame) tuples."""
    phases, start = [], 0
    for name, pattern, length in SCENE:
        phases.append((name, pattern, start, start + length))
        start += length
    return phases


def build_pipeline(args):
    trigger = f"gvatagger name=trigger k={args.k} margin={args.margin} min-baseline={args.min_baseline}"
    tail = f"{trigger} ! decodebin3 ! gvafpscounter ! identity name=tap ! "
    if args.output:
        # gvawatermark renders watermark meta only on BGR input, as in the watermark_meta sample
        tail += (
            f"videoconvert ! video/x-raw,format=BGR ! motion_overlay ! gvawatermark ! videoconvert ! "
            f'openh264enc ! h264parse ! mp4mux ! filesink sync=false location="{os.path.abspath(args.output)}"'
        )
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


def collect_tags(tap, frames):
    """Append (motion, keyframe) per decoded frame, or None if the meta is missing."""

    def on_buffer(_pad, info):
        meta = info.get_buffer().get_custom_meta(META_NAME)
        if meta is None:
            frames.append(None)
        else:
            s = meta.get_structure()
            frames.append((s.get_value("motion"), s.get_value("keyframe")))
        return Gst.PadProbeReturn.OK

    tap.get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, on_buffer)


def motion_segments(frames, gap):
    """Merge motion frames into [start, end) segments, bridging gaps of up to `gap` frames."""
    segments = []
    for i, tag in enumerate(frames):
        if not tag or not tag[0]:
            continue
        if segments and i - segments[-1][1] <= gap:
            segments[-1][1] = i + 1
        else:
            segments.append([i, i + 1])
    return segments


def print_summary(frames, phases):
    total = len(frames)
    tagged = [f for f in frames if f is not None]
    motion = sum(1 for f in tagged if f[0])
    keyframes = sum(1 for f in tagged if f[1])

    print("\n=== gvatagger motion summary ===")
    print(f"decoded frames      : {total}")
    print(f"frames with meta    : {len(tagged)}")
    print(f"keyframes           : {keyframes}")
    print(f"motion frames       : {motion} ({100.0 * motion / max(total, 1):.1f}%)")
    print(f"inference skippable : {total - motion} ({100.0 * (total - motion) / max(total, 1):.1f}%)")

    if phases:
        print(f"\n{'phase':<10}{'frames':>10}{'motion':>9}{'motion %':>10}")
        for name, _, start, end in phases:
            part = [f for f in frames[start:end] if f is not None]
            hits = sum(1 for f in part if f[0])
            print(f"{name:<10}{f'{start}-{end - 1}':>10}{hits:>9}{100.0 * hits / max(len(part), 1):>9.1f}%")

    segments = motion_segments(frames, gap=FPS // 2)
    print(f"\nmotion segments (gaps <= {FPS // 2} frames merged):")
    if not segments:
        print("  none")
    for start, end in segments:
        print(f"  {start / FPS:7.2f}s - {end / FPS:7.2f}s  ({end - start} frames)")


def main():
    parser = argparse.ArgumentParser(description="gvatagger coded-size motion tagging sample")
    parser.add_argument("--input", help="H.264/H.265 video file or URI; default is a synthetic scene")
    parser.add_argument("--encoder", choices=ENCODERS, default="openh264",
                        help="encoder for the synthetic scene (va* need an Intel GPU)")
    parser.add_argument("--k", type=float, default=1.5, help="motion threshold multiplier")
    parser.add_argument("--margin", type=int, default=8, help="minimum byte margin above baseline")
    parser.add_argument("--min-baseline", type=int, default=10, help="baseline floor in bytes")
    parser.add_argument("--output", help="write an MP4 with a red border and MOTION label on motion frames")
    parser.add_argument("--gst-debug", metavar="SPEC", default="*:3",
                        help="GStreamer debug/info logging level, same syntax as GST_DEBUG (default: '*:3')")
    args = parser.parse_args()

    if args.gst_debug:
        Gst.debug_set_active(True)
        Gst.debug_set_threshold_from_string(args.gst_debug, True)

    if args.output and not Gst.Element.register(None, "motion_overlay", Gst.Rank.NONE, MotionOverlay):
        print("Failed to register motion_overlay element")
        return 1

    pipeline = Gst.parse_launch(build_pipeline(args))

    phases = None
    if not args.input:
        phases = scene_phases()
        switch_patterns(pipeline.get_by_name("src"), phases)

    frames = []
    collect_tags(pipeline.get_by_name("tap"), frames)

    pipeline.set_state(Gst.State.PLAYING)
    msg = pipeline.get_bus().timed_pop_filtered(Gst.CLOCK_TIME_NONE, Gst.MessageType.EOS | Gst.MessageType.ERROR)
    pipeline.set_state(Gst.State.NULL)

    if msg.type == Gst.MessageType.ERROR:
        err, debug_info = msg.parse_error()
        print(f"Error from element {msg.src.get_name()}: {err.message}")
        print(f"Debug info: {debug_info}")
        return 1

    print_summary(frames, phases)
    if args.output:
        print(f"\nOutput written to: {os.path.abspath(args.output)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
