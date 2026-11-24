# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
DL Streamer sample: changing gvadetect properties while the pipeline is running.

`threshold` and `inference-interval` of gvadetect (and `inference-interval` of the other inference elements) can be
changed at any time without restarting the pipeline. This sample runs a detection pipeline and, from a separate thread,
steps through a schedule of changes, measuring the detections of each phase:

    1. baseline                     threshold 0.5, inference on every frame
    2. higher threshold             threshold 0.9  -> fewer, only the most confident detections
    3. lower threshold              threshold 0.1  -> more detections, including uncertain ones
    4. every 5th frame              threshold 0.5, inference-interval 5 -> detections only on the inferred frames
    5. back to the start            inference-interval 1

At the start of each phase the input is rewound, so that every phase sees the same frames and the numbers can be
compared. Only the playback position goes back: the pipeline, the element and the model keep running.

Then it asks for changes that must be refused (out-of-range values, properties that cannot be changed while running,
unknown properties) and shows the status that comes back for each of them.

With --interactive there is no schedule: the changes are typed on the standard input and the detections of the last
second are printed every second.

Pipeline:
    filesrc -> decodebin3 -> gvadetect -> fakesink

Supports file and RTSP inputs.
"""

import argparse
import os
import signal
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")

from gi.repository import Gst  # pylint: disable=no-name-in-module, wrong-import-position
from gstgva import VideoFrame, util  # pylint: disable=wrong-import-position

# The model is prepared with the model conversion scripts of DL Streamer (scripts/download_models/), which write
# ${MODELS_PATH}/public/<model>/<precision>/<model>.xml
DEFAULT_MODEL_PATH = Path("public") / "yolo11s" / "FP16" / "yolo11s.xml"

INITIAL_THRESHOLD = 0.5
INITIAL_INTERVAL = 1

# The first moments of a phase are not counted: the pipeline is still refilling after the rewind
SETTLE_SECONDS = 0.5

# Status of the "set-runtime-property" signal, see docs/user-guide/elements/gvadetect.md
STATUS_APPLIED = 0


@dataclass
class Change:
    """A property change of the schedule. `direct` uses g_object_set() instead of the signal."""

    name: str
    value: object
    direct: bool = False


# (title, changes applied at the start of the phase)
PHASES = [
    ("baseline", []),
    ("higher threshold", [Change("threshold", 0.9)]),
    ("lower threshold", [Change("threshold", 0.1)]),
    ("every 5th frame", [Change("threshold", 0.5), Change("inference-interval", 5)]),
    ("back to the start", [Change("inference-interval", 1, direct=True)]),
]

# (property, value, why it is refused). The values are sent as text, like a REST API would.
REFUSED_REQUESTS = [
    ("threshold", "1.5", "above the range 0..1"),
    ("threshold", "abc", "not a number"),
    ("inference-interval", "0", "below the minimum of 1"),
    ("device", "CPU", "read once, when the element starts"),
    ("batch-size", "4", "read once, when the element starts"),
    ("no-such-property", "1", "there is no such property"),
]


# ── statistics ───────────────────────────────────────────────────────────────


@dataclass
class Stats:
    """Detections seen while one set of property values was in effect."""

    label: str
    frames: int = 0
    frames_with_detections: int = 0
    detections: int = 0
    min_confidence: float = float("inf")


class Collector:
    """Collects the detections of the frames that leave gvadetect. Called from the streaming thread."""

    def __init__(self):
        self._lock = threading.Lock()
        self._current = Stats("")
        self._ignore_until = 0.0
        self.first_frame = threading.Event()

    def on_frame(self, confidences):
        self.first_frame.set()
        with self._lock:
            if time.monotonic() < self._ignore_until:
                return
            stats = self._current
            stats.frames += 1
            if confidences:
                stats.frames_with_detections += 1
                stats.detections += len(confidences)
                stats.min_confidence = min(stats.min_confidence, min(confidences))

    def rotate(self, label, settle=0.0):
        """Returns what was collected so far and starts collecting for `label`."""
        with self._lock:
            finished = self._current
            self._current = Stats(label)
            self._ignore_until = time.monotonic() + settle
        return finished


def make_probe(collector):
    def probe(pad, info):
        with util.GST_PAD_PROBE_INFO_BUFFER(info) as buffer:
            frame = VideoFrame(buffer, caps=pad.get_current_caps())
            collector.on_frame([roi.confidence() for roi in frame.regions()])
        return Gst.PadProbeReturn.OK

    return probe


def format_stats(stats):
    per_frame = stats.detections / stats.frames if stats.frames else 0.0
    min_conf = f"{stats.min_confidence:.2f}" if stats.detections else "-"
    return stats.frames, stats.frames_with_detections, stats.detections, per_frame, min_conf


def print_report(results):
    if not results:
        print("No detections were collected.")
        return
    width = max(len("Phase"), max(len(r.label) for r in results))
    header = f"{'Phase':<{width}}  {'Frames':>6}  {'With det.':>9}  {'Detections':>10}  {'Det./frame':>10}  {'Min conf.':>9}"
    print("\n" + header)
    print("-" * len(header))
    for stats in results:
        frames, with_det, dets, per_frame, min_conf = format_stats(stats)
        print(f"{stats.label:<{width}}  {frames:>6}  {with_det:>9}  {dets:>10}  {per_frame:>10.2f}  {min_conf:>9}")
    print()


# ── changing properties ──────────────────────────────────────────────────────


def change_with_signal(element, name, value):
    """Changes a property of the running element and reports what the element answered.

    The "set-runtime-property" action signal takes the value as text and returns a Gst.Structure with the fields
    status, code, message, property, previous and current. A refused request changes nothing.
    """
    result = element.emit("set-runtime-property", name, str(value))
    status = result.get_value("status")
    code = result.get_value("code")
    if status == STATUS_APPLIED:
        print(f"[signal]  {name}: {result.get_value('previous')} -> {result.get_value('current')}  ({code})")
    else:
        print(f"[signal]  {name}={value!r} refused: {code} - {result.get_value('message')}")
    return status == STATUS_APPLIED


def change_directly(element, name, value):
    """Changes a property with g_object_set(). It takes effect the same way, but there is no status to look at,
    so the value is read back."""
    previous = element.get_property(name)
    element.set_property(name, value)
    print(f"[direct]  {name}: {previous} -> {element.get_property(name)}  (set_property)")


def rewind(pipeline):
    """Plays the input from the start again, so that every phase sees the same frames. Nothing is restarted."""
    return pipeline.seek_simple(Gst.Format.TIME, Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT, 0)


def describe(element):
    return f"threshold={element.get_property('threshold'):.2f}, inference-interval={element.get_property('inference-interval')}"


class Scenario(threading.Thread):
    """Applies the schedule of PHASES to a running gvadetect, one phase every `phase_seconds`."""

    def __init__(self, pipeline, collector, phase_seconds, stop):
        super().__init__(daemon=True)
        self.pipeline = pipeline
        self.detect = pipeline.get_by_name("detect")
        self.collector = collector
        self.phase_seconds = phase_seconds
        self.stop = stop
        self.results = []
        self.completed = False

    def _close_phase(self, next_label):
        finished = self.collector.rotate(next_label, SETTLE_SECONDS)
        if finished.label:
            self.results.append(finished)

    def _show_refused_requests(self):
        print("\nRequests that must be refused:")
        for name, value, why in REFUSED_REQUESTS:
            print(f"  ({why})")
            print("  ", end="")
            change_with_signal(self.detect, name, value)
        print(f"Nothing was changed: {describe(self.detect)}, batch-size={self.detect.get_property('batch-size')}\n")

    def run(self):
        # The first frame only arrives after the model was compiled, which can take a while
        while not self.collector.first_frame.wait(0.2):
            if self.stop.is_set():
                return

        rewindable = True
        for index, (title, changes) in enumerate(PHASES):
            print(f"\n=== {title} ===")
            for change in changes:
                if change.direct:
                    change_directly(self.detect, change.name, change.value)
                else:
                    change_with_signal(self.detect, change.name, change.value)
            # The first phase starts with the first frame. A flushing seek also drops what the old values were
            # still working on
            if index > 0 and rewindable and not rewind(self.pipeline):
                rewindable = False
                print("The input cannot be rewound: the phases see different frames, so compare them with care.")
            self._close_phase(f"{title} ({describe(self.detect)})")
            if self.stop.wait(self.phase_seconds):
                break
        else:
            self.completed = True
            self._show_refused_requests()

        self._close_phase("")
        # The schedule is over, or the pipeline already ended
        self.pipeline.send_event(Gst.Event.new_eos())


# ── interactive mode ─────────────────────────────────────────────────────────

HELP = """Commands:
  threshold <0..1>       change the confidence threshold
  interval <N>           change the inference interval (1 = every frame)
  set <property> <value> try any property, to see how it is refused
  show                   print the current values
  quit                   stop the pipeline
"""


class LiveReport(threading.Thread):
    """Prints the detections of the last second, with the values that are in effect."""

    def __init__(self, detect, collector, stop):
        super().__init__(daemon=True)
        self.detect = detect
        self.collector = collector
        self.stop = stop

    def run(self):
        while not self.stop.wait(1.0):
            stats = self.collector.rotate("")
            frames, with_det, dets, _, min_conf = format_stats(stats)
            print(
                f"[live] {describe(self.detect)} | last second: frames={frames} with detections={with_det} "
                f"detections={dets} min confidence={min_conf}"
            )


class CommandReader(threading.Thread):
    """Reads the changes from the standard input."""

    def __init__(self, pipeline, detect):
        super().__init__(daemon=True)
        self.pipeline = pipeline
        self.detect = detect

    def run(self):
        print(HELP)
        for line in sys.stdin:
            words = line.split()
            if not words:
                continue
            command = words[0].lower()
            if command in ("quit", "exit", "q"):
                self.pipeline.send_event(Gst.Event.new_eos())
                return
            if command == "show":
                print(describe(self.detect))
            elif command == "threshold" and len(words) == 2:
                change_with_signal(self.detect, "threshold", words[1])
            elif command == "interval" and len(words) == 2:
                change_with_signal(self.detect, "inference-interval", words[1])
            elif command == "set" and len(words) == 3:
                change_with_signal(self.detect, words[1], words[2])
            else:
                print(HELP)


# ── pipeline ─────────────────────────────────────────────────────────────────


def parse_args():
    parser = argparse.ArgumentParser(description="DL Streamer: change gvadetect properties while running")
    parser.add_argument("--input", required=True, help="Video file path or rtsp:// URI")
    parser.add_argument(
        "--model",
        default=None,
        help=f"Detection model (.xml). Default: $MODELS_PATH/{DEFAULT_MODEL_PATH.as_posix()}",
    )
    parser.add_argument("--device", default="GPU", help="Inference device (default: GPU)")
    parser.add_argument(
        "--phase-seconds",
        type=float,
        default=6.0,
        help="How long each phase of the schedule lasts (default: 6). The input must be long enough",
    )
    parser.add_argument(
        "--interactive",
        action="store_true",
        help="Instead of the schedule, change the properties by typing commands",
    )
    args = parser.parse_args()
    if args.phase_seconds <= SETTLE_SECONDS:
        parser.error(f"--phase-seconds must be more than {SETTLE_SECONDS}")
    return args


def validate_input(source):
    """Validate video input path or RTSP URI."""
    if source.startswith("rtsp://"):
        return source
    if not os.path.isfile(source):
        sys.stderr.write(f"Error: file not found: {source}\n")
        sys.exit(1)
    return os.path.abspath(source)


def find_model(requested):
    """The model given with --model, or the default one under $MODELS_PATH."""
    if requested:
        path = Path(requested)
    else:
        models_path = os.environ.get("MODELS_PATH")
        if not models_path:
            sys.stderr.write("Error: MODELS_PATH is not set. Set it to the directory the models were downloaded to,\n")
            sys.stderr.write("or give the model with --model. See README.md, section Models.\n")
            sys.exit(1)
        path = Path(models_path) / DEFAULT_MODEL_PATH
    if not path.is_file():
        sys.stderr.write(f"Error: model not found: {path}\n")
        sys.stderr.write("Prepare it with the model conversion scripts, see README.md, section Models.\n")
        sys.exit(1)
    return str(path.resolve())


def check_device(requested):
    """Fall back from NPU/GPU to what is available."""
    if requested == "NPU" and not os.path.exists("/dev/accel/accel0"):
        print("Warning: NPU not available, falling back to GPU")
        requested = "GPU"
    if requested == "GPU" and not os.path.exists("/dev/dri/renderD128"):
        print("Warning: GPU not available, falling back to CPU")
        requested = "CPU"
    return requested


def build_pipeline(source, model, device):
    src = f"rtspsrc location={source} latency=100" if source.startswith("rtsp://") else f'filesrc location="{source}"'
    # sync=true plays the video at its own speed, so that a phase lasts as many seconds as it says
    return (
        f'{src} ! decodebin3 caps="video/x-raw(ANY)" ! '
        f'gvadetect name=detect model="{model}" device={device} batch-size=1 '
        f"threshold={INITIAL_THRESHOLD} inference-interval={INITIAL_INTERVAL} ! "
        "fakesink sync=true"
    )


def run_pipeline(pipeline, stop):
    """Event loop with SIGINT -> EOS."""

    def _sigint(signum, frame):
        del signum, frame
        pipeline.send_event(Gst.Event.new_eos())

    previous = signal.signal(signal.SIGINT, _sigint)
    bus = pipeline.get_bus()
    print("[pipeline] Compiling the model, this may take some time...")
    pipeline.set_state(Gst.State.PLAYING)
    try:
        while True:
            msg = bus.timed_pop_filtered(100 * Gst.MSECOND, Gst.MessageType.ERROR | Gst.MessageType.EOS)
            if msg is None:
                continue
            if msg.type == Gst.MessageType.ERROR:
                err, debug = msg.parse_error()
                print(f"Error from {msg.src.get_name()}: {err.message}\nDebug: {debug}")
                return False
            print("Pipeline complete.")
            return True
    finally:
        stop.set()
        signal.signal(signal.SIGINT, previous)
        pipeline.set_state(Gst.State.NULL)


def main():
    args = parse_args()
    source = validate_input(args.input)
    model = find_model(args.model)
    device = check_device(args.device)

    Gst.init([])
    pipeline = Gst.parse_launch(build_pipeline(source, model, device))
    detect = pipeline.get_by_name("detect")

    collector = Collector()
    detect.get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, make_probe(collector))

    stop = threading.Event()
    if args.interactive:
        scenario = None
        CommandReader(pipeline, detect).start()
        LiveReport(detect, collector, stop).start()
    else:
        scenario = Scenario(pipeline, collector, args.phase_seconds, stop)
        scenario.start()

    ok = run_pipeline(pipeline, stop)

    if scenario:
        scenario.join()
        print_report(scenario.results)
        if ok and not scenario.completed:
            print(
                "The input ended before the schedule did: the last phase is partial, the phases after it did not "
                "run and the refused requests were not shown. Use a longer input or a smaller --phase-seconds."
            )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
