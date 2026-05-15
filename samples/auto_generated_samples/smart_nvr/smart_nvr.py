# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Smart NVR — Event-based smart video recording pipeline.

Pipeline:
    source → decodebin3 →
    gvadetect (person detection) → queue →
    gvapersondetect_py (person presence analytics) →
    gvafpscounter → gvawatermark →
    gvaeventrecorder_py (event-triggered recording to save-1.mp4, save-2.mp4, ...)

Detects people in camera view and records video segments only when a person
is visible. Supports file and RTSP IP camera inputs.
Optimized for Intel Core Ultra 3 processors.
"""

import argparse
import os
import signal
import subprocess
import sys
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")

SCRIPT_DIR = Path(__file__).resolve().parent
MODELS_DIR = SCRIPT_DIR / "models"
RESULTS_DIR = SCRIPT_DIR / "results"
VIDEOS_DIR = SCRIPT_DIR / "videos"

DEFAULT_VIDEO = str(VIDEOS_DIR / "person_walking.mp4")


# ── helpers ──────────────────────────────────────────────────────────────────


def parse_args():
    p = argparse.ArgumentParser(description="Smart NVR — Event-based video recording")
    p.add_argument(
        "--input",
        default=DEFAULT_VIDEO,
        help="Video file path or rtsp:// URI (default: videos/person_walking.mp4)",
    )
    p.add_argument("--device", default="GPU", help="Inference device (default: GPU)")
    p.add_argument(
        "--output",
        default=str(RESULTS_DIR / "save"),
        help="Base path for output files (default: results/save → save-1.mp4, save-2.mp4, ...)",
    )
    p.add_argument(
        "--threshold", type=float, default=0.5, help="Detection confidence threshold"
    )
    p.add_argument(
        "--cooldown",
        type=int,
        default=15,
        help="Frames to keep recording after person leaves view (default: 15)",
    )
    p.add_argument(
        "--loop",
        type=int,
        default=1,
        help="Number of times to loop input video (0=infinite, default: 1)",
    )
    return p.parse_args()


def validate_input(source: str) -> str:
    """Validate video input path or RTSP URI."""
    if source.startswith("rtsp://"):
        return source
    if not os.path.isfile(source):
        sys.stderr.write(f"Error: file not found: {source}\n")
        sys.stderr.write("Download the test video first — see README.md\n")
        sys.exit(1)
    return os.path.abspath(source)


def find_model(pattern: str, label: str) -> str:
    """Glob for a model .xml inside MODELS_DIR."""
    hits = sorted(MODELS_DIR.glob(pattern))
    if not hits:
        sys.stderr.write(f"Error: {label} model not found. Run: python3 export_models.py\n")
        sys.exit(1)
    return str(hits[0])


def check_device(requested: str, label: str) -> str:
    """Check device availability with fallback chain: NPU → GPU → CPU."""
    if requested == "NPU" and not os.path.exists("/dev/accel/accel0"):
        print(f"Warning: NPU not available for {label}, falling back to GPU")
        requested = "GPU"
    if requested == "GPU" and not os.path.exists("/dev/dri/renderD128"):
        print(f"Warning: GPU not available for {label}, falling back to CPU")
        requested = "CPU"
    return requested


def build_source(src: str) -> str:
    """Build GStreamer source element string for file or RTSP."""
    if src.startswith("rtsp://"):
        return f"rtspsrc location={src} latency=100"
    return f'filesrc location="{src}"'


def ensure_models():
    """Export models if not already present."""
    xml_files = list(MODELS_DIR.glob("**/*.xml")) if MODELS_DIR.exists() else []
    if not xml_files:
        print("Models not found. Running export_models.py...")
        result = subprocess.run(
            [sys.executable, str(SCRIPT_DIR / "export_models.py")],
            check=False,
        )
        if result.returncode != 0:
            sys.stderr.write("Model export failed.\n")
            sys.exit(1)


# ── pipeline event loop ─────────────────────────────────────────────────────

from gi.repository import GLib, Gst  # noqa: E402


def run_pipeline(pipeline, loop_count=1):
    """Event loop with SIGINT → EOS, optional looping."""
    remaining = loop_count - 1  # -1 means infinite when loop_count == 0

    def _sigint(signum, frame):
        nonlocal remaining
        remaining = 0
        pipeline.send_event(Gst.Event.new_eos())

    prev = signal.signal(signal.SIGINT, _sigint)
    bus = pipeline.get_bus()

    print("[pipeline] Compiling models, this may take some time...")
    pipeline.set_state(Gst.State.PAUSED)
    ret = pipeline.get_state(Gst.CLOCK_TIME_NONE)
    if ret[0] == Gst.StateChangeReturn.FAILURE:
        pipeline.set_state(Gst.State.NULL)
        raise RuntimeError("Pipeline failed to reach PAUSED")

    pipeline.set_state(Gst.State.PLAYING)
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
                raise RuntimeError(f"Pipeline error: {err.message}\nDebug: {dbg}")
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


# ── main ─────────────────────────────────────────────────────────────────────


def main():
    args = parse_args()

    # Register custom Python GStreamer elements
    plugins_dir = str(SCRIPT_DIR / "plugins")
    if plugins_dir not in os.environ.get("GST_PLUGIN_PATH", ""):
        os.environ["GST_PLUGIN_PATH"] = (
            f"{os.environ.get('GST_PLUGIN_PATH', '')}:{plugins_dir}"
        )
    os.environ.setdefault("GST_REGISTRY_FORK", "no")

    Gst.init(None)
    reg = Gst.Registry.get()
    if not reg.find_plugin("python"):
        sys.stderr.write(
            "GStreamer 'python' plugin not found.\n"
            "Install gst-python / python3-gst-1.0 and clear "
            "~/.cache/gstreamer-1.0/registry.*.bin if needed.\n"
        )
        sys.exit(1)

    # Validate input
    input_src = validate_input(args.input)

    # Ensure models are exported
    ensure_models()

    # Locate detection model
    model_xml = find_model("**/*.xml", "detection")

    # Create output directory
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)

    # Device fallback
    device = check_device(args.device, "inference")

    # Build pipeline
    source_el = build_source(input_src)

    pipe = (
        f'{source_el} ! decodebin3 caps="video/x-raw(ANY)" ! '
        f'gvadetect model="{model_xml}" device={device} '
        f"batch-size=4 threshold={args.threshold} ! queue ! "
        f"gvapersondetect_py cooldown={args.cooldown} ! "
        f"gvafpscounter ! gvawatermark ! "
        f'gvaeventrecorder_py location="{args.output}"'
    )

    print(f"\nPipeline:\n{pipe}\n")
    pipeline = Gst.parse_launch(pipe)

    run_pipeline(pipeline, loop_count=args.loop)

    # Report output files
    results = sorted(Path(args.output).parent.glob("save-*.mp4"))
    if results:
        print(f"\nRecorded {len(results)} segment(s):")
        for f in results:
            print(f"  {f}")
    else:
        print("\nNo person detected — no recordings created.")


if __name__ == "__main__":
    main()
