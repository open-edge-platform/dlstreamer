# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
DL Streamer Multi-Stream Compose pipeline.

Processes N video streams with shared YOLO detection, composites them into
a 2x2 mosaic via vacompositor, streams via WebRTC, and supports per-stream
on-demand recording controlled via stdin commands.

Pipeline (per stream):
    filesrc → decodebin3 →
    gvadetect (shared model) → queue → gvafpscounter → gvawatermark →
    tee name=stream_tee_N
      → vapostproc (resize) → queue → vacompositor.sink_N
      → valve → videoconvert → vah264enc → h264parse →
        mp4mux fragment-duration=1000 → filesink (async=false)

Compositor output:
    vacompositor → videoconvert → webrtcsink

Commands (stdin):
    record <N>   — start recording stream N
    stop         — stop recording
    quit         — graceful shutdown
"""

import argparse
import os
import signal
import sys
import threading
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")

from gi.repository import GLib, Gst  # pylint: disable=no-name-in-module, wrong-import-position

SCRIPT_DIR = Path(__file__).resolve().parent
MODELS_DIR = SCRIPT_DIR / "models"
RESULTS_DIR = SCRIPT_DIR / "results"

DEFAULT_VIDEO = str(SCRIPT_DIR / "videos" / "sample_traffic.mp4")

TILE_W = 640
TILE_H = 360


# ── helpers ──────────────────────────────────────────────────────────────────


def parse_args():
    p = argparse.ArgumentParser(description="DL Streamer Multi-Stream Compose")
    p.add_argument(
        "--input",
        nargs="+",
        default=None,
        help="Video file paths or rtsp:// URIs (one per stream). "
             "If fewer inputs than --num-streams, inputs are repeated.",
    )
    p.add_argument("--num-streams", type=int, default=4, help="Number of streams (default: 4)")
    p.add_argument("--device", default="GPU", help="Inference device (default: GPU)")
    p.add_argument("--webrtc-port", type=int, default=8443, help="WebRTC signalling port (default: 8443)")
    p.add_argument("--loop", type=int, default=0, help="Loop count: 0=infinite, N=play N times (default: 0)")
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


# ── Recording Controller ────────────────────────────────────────────────────


class RecordingController:
    """Control per-stream recording valves."""

    def __init__(self, pipeline, num_streams):
        self._valves = [pipeline.get_by_name(f"rec_valve_{i}") for i in range(num_streams)]
        self._active = -1
        self._num_streams = num_streams

    def close_all_valves(self):
        for v in self._valves:
            v.set_property("drop", True)

    def start(self, idx):
        if idx < 0 or idx >= self._num_streams:
            print(f"Invalid stream index: {idx} (0-{self._num_streams - 1})")
            return
        self.stop()
        self._active = idx
        self._valves[idx].set_property("drop", False)
        print(f"Recording stream {idx}")

    def stop(self):
        if self._active >= 0:
            self._valves[self._active].set_property("drop", True)
            print(f"Stopped recording stream {self._active}")
            self._active = -1


# ── Command Reader ──────────────────────────────────────────────────────────


class CommandReader:
    """Read commands from stdin and dispatch to the GLib main loop."""

    def __init__(self, pipeline, controller):
        self.pipeline = pipeline
        self.controller = controller
        self.shutdown_requested = False
        self._commands = {
            "record": self._record,
            "stop": self._stop,
            "quit": self._quit,
        }

    def start(self):
        thread = threading.Thread(target=self._read_loop, daemon=True)
        thread.start()
        print("\nCommands: record <N>, stop, quit")

    def _read_loop(self):
        try:
            for line in sys.stdin:
                parts = line.strip().lower().split()
                if not parts:
                    continue
                handler = self._commands.get(parts[0])
                if handler:
                    GLib.idle_add(handler, *parts[1:])
                else:
                    print(f"Unknown command: {parts[0]}. Use: record <N>, stop, quit")
        except EOFError:
            pass

    def _record(self, *args):
        if not args:
            print("Usage: record <stream_index>")
            return GLib.SOURCE_REMOVE
        try:
            idx = int(args[0])
        except ValueError:
            print(f"Invalid stream index: {args[0]}")
            return GLib.SOURCE_REMOVE
        self.controller.start(idx)
        return GLib.SOURCE_REMOVE

    def _stop(self, *args):
        self.controller.stop()
        return GLib.SOURCE_REMOVE

    def _quit(self, *args):
        self.shutdown_requested = True
        self.pipeline.send_event(Gst.Event.new_eos())
        return GLib.SOURCE_REMOVE


# ── Pipeline Event Loop ─────────────────────────────────────────────────────


def run_pipeline(pipeline, controller, cmd_reader, loop_count=0):
    remaining = loop_count - 1  # -1 means infinite when loop_count == 0

    def _sigint_handler(signum, frame):
        nonlocal remaining
        remaining = 0
        pipeline.send_event(Gst.Event.new_eos())

    prev = signal.signal(signal.SIGINT, _sigint_handler)
    bus = pipeline.get_bus()

    print("[pipeline] Compiling models, this may take some time...")
    pipeline.set_state(Gst.State.PAUSED)
    ret = pipeline.get_state(Gst.CLOCK_TIME_NONE)
    if ret[0] == Gst.StateChangeReturn.FAILURE:
        pipeline.set_state(Gst.State.NULL)
        raise RuntimeError("Pipeline failed to reach PAUSED")

    pipeline.set_state(Gst.State.PLAYING)

    # Close all recording valves once preroll is done
    controller.close_all_valves()

    try:
        while True:
            while GLib.MainContext.default().iteration(False):
                pass

            msg = bus.timed_pop_filtered(
                100 * Gst.MSECOND,
                Gst.MessageType.ERROR | Gst.MessageType.EOS,
            )

            if cmd_reader and cmd_reader.shutdown_requested and msg is None:
                break

            if msg is None:
                continue
            if msg.type == Gst.MessageType.ERROR:
                err, debug = msg.parse_error()
                raise RuntimeError(f"Pipeline error: {err.message}\nDebug: {debug}")
            if msg.type == Gst.MessageType.EOS:
                if remaining != 0:
                    if remaining > 0:
                        remaining -= 1
                    print(f"Looping input ({remaining if remaining >= 0 else '∞'} remaining)...")
                    pipeline.seek_simple(
                        Gst.Format.TIME,
                        Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT,
                        0,
                    )
                    continue
                print("Pipeline complete.")
                break
    finally:
        signal.signal(signal.SIGINT, prev)
        pipeline.set_state(Gst.State.NULL)


# ── Pipeline Builder ────────────────────────────────────────────────────────


def build_pipeline(sources, model_xml, device, num_streams, webrtc_port):
    """Build the multi-stream compositor pipeline string."""
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    # Check if webrtcsink is available
    use_webrtc = Gst.ElementFactory.find("webrtcsink") is not None
    if not use_webrtc:
        print("Warning: webrtcsink not available, falling back to autovideosink")

    # Compositor with 2x2 grid layout
    comp_parts = ["vacompositor name=comp"]
    for i in range(num_streams):
        col = i % 2
        row = i // 2
        xpos = col * TILE_W
        ypos = row * TILE_H
        comp_parts.append(f"sink_{i}::xpos={xpos} sink_{i}::ypos={ypos}")

    if use_webrtc:
        comp_output = (
            f" ! videoconvert ! "
            f"webrtcsink name=ws run-signalling-server=true run-web-server=true "
            f"signalling-server-port={webrtc_port}"
        )
    else:
        comp_output = " ! videoconvert ! autovideosink sync=true"

    comp_str = " ".join(comp_parts) + comp_output

    # Per-stream pipeline fragments
    stream_parts = []
    for i, src in enumerate(sources):
        source_el = build_source(src)
        rec_file = str(RESULTS_DIR / f"stream_{i}.mp4")
        stream = (
            f'{source_el} ! decodebin3 caps="video/x-raw(ANY)" ! '
            f'gvadetect model="{model_xml}" device={device} '
            f"model-instance-id=detect0 batch-size={num_streams} "
            f"scheduling-policy=latency ! "
            f"queue flush-on-eos=true ! "
            f"gvafpscounter ! gvawatermark ! "
            f"tee name=stream_tee_{i} "
            # Branch 1: mosaic compositor
            f"stream_tee_{i}. ! queue flush-on-eos=true ! "
            f"vapostproc ! video/x-raw(memory:VAMemory),width={TILE_W},height={TILE_H} ! "
            f"queue ! comp.sink_{i} "
            # Branch 2: on-demand recording
            f"stream_tee_{i}. ! queue flush-on-eos=true ! "
            f"valve name=rec_valve_{i} drop=false ! "
            f"videoconvert ! vah264enc ! h264parse ! "
            f"mp4mux fragment-duration=1000 ! "
            f'filesink location="{rec_file}" async=false'
        )
        stream_parts.append(stream)

    return comp_str + " " + " ".join(stream_parts)


# ── main ─────────────────────────────────────────────────────────────────────


def main():
    args = parse_args()

    # Resolve inputs
    if args.input is None:
        args.input = [DEFAULT_VIDEO]
    validated = [validate_input(s) for s in args.input]
    # Repeat inputs to fill num_streams
    sources = []
    for i in range(args.num_streams):
        sources.append(validated[i % len(validated)])

    # Locate model
    model_xml = find_model("**/*.xml", "detection")

    # Device fallback
    device = check_device(args.device, "inference")

    Gst.init(None)

    pipe_str = build_pipeline(sources, model_xml, device, args.num_streams, args.webrtc_port)
    print(f"\nPipeline:\n{pipe_str}\n")

    pipeline = Gst.parse_launch(pipe_str)

    controller = RecordingController(pipeline, args.num_streams)
    cmd_reader = CommandReader(pipeline, controller)
    cmd_reader.start()

    run_pipeline(pipeline, controller, cmd_reader, loop_count=args.loop)

    print(f"\nRecordings saved in: {RESULTS_DIR}")


if __name__ == "__main__":
    main()
