# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
DL Streamer Safety Compliance monitoring pipeline.

Pipeline:
    filesrc → decodebin3 → gvadetect (YOLO26m) → gvatrack → tee
      Branch 1: gvawatermark → vah264enc → mp4mux → filesink (annotated video)
      Branch 2: gvaworkerselection_py → videoconvert → gvaworkercrop_py →
                videoconvertscale → gvagenai (Qwen2.5-VL) → save crops + JSON

Detects workers via YOLO, tracks them, selects frames for VLM safety checks,
crops individual worker regions, and sends them to a VLM for helmet/harness
compliance verification. Generates alerts for clear violations.
"""

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
gi.require_version("GObject", "2.0")
from gi.repository import GLib, Gst, GstAnalytics  # pylint: disable=wrong-import-position

SCRIPT_DIR = Path(__file__).resolve().parent
MODELS_DIR = SCRIPT_DIR / "models"
RESULTS_DIR = SCRIPT_DIR / "results"
VIDEOS_DIR = SCRIPT_DIR / "videos"

DEFAULT_VIDEO = str(VIDEOS_DIR / "construction_workers.mp4")

VLM_PROMPT_FILE = SCRIPT_DIR / "config" / "safety_prompt.txt"


# ── helpers ──────────────────────────────────────────────────────────────────


def parse_args():
    p = argparse.ArgumentParser(description="DL Streamer Safety Compliance Monitor")
    p.add_argument("--input", default=DEFAULT_VIDEO, help="Video file path or rtsp:// URI")
    p.add_argument("--detect-device", default="GPU", help="Device for YOLO detection")
    p.add_argument("--vlm-device", default="GPU", help="Device for VLM inference")
    p.add_argument("--threshold", type=float, default=0.5, help="Detection confidence threshold")
    p.add_argument("--recheck-interval", type=int, default=30,
                   help="Seconds between VLM rechecks for tracked workers")
    p.add_argument("--output-video", default=str(RESULTS_DIR / "output.mp4"))
    p.add_argument("--output-json", default=str(RESULTS_DIR / "vlm_checks.json"))
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


# ── VLM results tracking ────────────────────────────────────────────────────


class VLMResultsTracker:
    """Track VLM check results and generate alerts."""

    def __init__(self, output_json: str, output_snapshots: str):
        self.output_json = output_json
        self.output_snapshots = output_snapshots
        self.checks = []
        self.check_count = 0
        self.alert_count = 0

    def process_vlm_response(self, response: str, pts: int):
        """Parse VLM response and generate alert if violation detected."""
        self.check_count += 1
        response = response.strip()

        # Parse response: expected format "WEARING / SECURED" or similar
        parts = [p.strip().upper() for p in response.split("/")]
        helmet_status = parts[0] if len(parts) >= 1 else "UNCERTAIN"
        harness_status = parts[1] if len(parts) >= 2 else "UNCERTAIN"

        timestamp_s = pts / Gst.SECOND if pts != Gst.CLOCK_TIME_NONE else 0

        record = {
            "check_id": self.check_count,
            "timestamp_s": round(timestamp_s, 2),
            "vlm_response": response,
            "helmet": helmet_status,
            "harness": harness_status,
            "alert": False,
            "alert_reason": [],
        }

        # Generate alerts only for clear violations
        alerts = []
        if helmet_status == "NOT_WEARING":
            alerts.append("helmet: NOT_WEARING")
        if harness_status == "NOT_SECURED":
            alerts.append("harness: NOT_SECURED")

        if alerts:
            record["alert"] = True
            record["alert_reason"] = alerts
            self.alert_count += 1
            print(f"\n*** ALERT #{self.alert_count} at {timestamp_s:.2f}s: {', '.join(alerts)} ***")
            print(f"    VLM response: {response}")

        self.checks.append(record)
        self._save_json()
        return record

    def _save_json(self):
        Path(self.output_json).parent.mkdir(parents=True, exist_ok=True)
        with open(self.output_json, "w") as f:
            json.dump({"total_checks": self.check_count,
                        "total_alerts": self.alert_count,
                        "checks": self.checks}, f, indent=2)

    def print_summary(self):
        print(f"\n{'='*60}")
        print(f"Safety Compliance Summary")
        print(f"{'='*60}")
        print(f"Total VLM checks: {self.check_count}")
        print(f"Total alerts:     {self.alert_count}")
        for c in self.checks:
            status = "ALERT" if c["alert"] else "OK"
            print(f"  Check #{c['check_id']} at {c['timestamp_s']:.2f}s: "
                  f"helmet={c['helmet']}, harness={c['harness']} [{status}]")
        print(f"{'='*60}")


# ── Pad probe callbacks ─────────────────────────────────────────────────────


def _post_vlm_cb(pad, info, tracker):
    """Probe on gvagenai src pad: extract VLM response and process it."""
    buf = info.get_buffer()
    if buf is None:
        return Gst.PadProbeReturn.OK

    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buf)
    if not rmeta:
        return Gst.PadProbeReturn.OK

    for mtd in rmeta:
        if isinstance(mtd, GstAnalytics.ClsMtd) and mtd.get_quark(0):
            response = GLib.quark_to_string(mtd.get_quark(0))
            tracker.process_vlm_response(response, buf.pts)
            break

    return Gst.PadProbeReturn.OK


def _pre_watermark_cb(pad, info, tracker):
    """Probe on gvawatermark sink pad: add overlay for recent alerts."""
    buf = info.get_buffer()
    if buf is None:
        return Gst.PadProbeReturn.OK

    if not tracker.checks:
        return Gst.PadProbeReturn.OK

    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buf)
    if not rmeta:
        return Gst.PadProbeReturn.OK

    # Show recent alerts in overlay (last 4 seconds)
    ts_s = buf.pts / Gst.SECOND if buf.pts != Gst.CLOCK_TIME_NONE else 0
    y_pos = 50
    for check in reversed(tracker.checks[-5:]):
        age = ts_s - check["timestamp_s"]
        if age > 4.0 or age < 0:
            continue
        if check["alert"]:
            text = f"ALERT: {', '.join(check['alert_reason'])} (t={check['timestamp_s']:.1f}s)"
        else:
            text = f"OK: helmet={check['helmet']}, harness={check['harness']} (t={check['timestamp_s']:.1f}s)"
        rmeta.add_od_mtd(GLib.quark_from_string(text), 10, y_pos, 0, 0, 1.0)
        y_pos += 40

    return Gst.PadProbeReturn.OK


# ── Pipeline ─────────────────────────────────────────────────────────────────


def setup_gst_plugins():
    plugins_dir = str(SCRIPT_DIR / "plugins")
    if plugins_dir not in os.environ.get("GST_PLUGIN_PATH", ""):
        existing = os.environ.get("GST_PLUGIN_PATH", "")
        os.environ["GST_PLUGIN_PATH"] = f"{existing}:{plugins_dir}" if existing else plugins_dir
    os.environ.setdefault("GST_REGISTRY_FORK", "no")

    Gst.init(None)

    reg = Gst.Registry.get()
    if not reg.find_plugin("python"):
        raise RuntimeError(
            "GStreamer 'python' plugin not found. "
            "Install gst-python / python3-gst-1.0 and clear "
            "~/.cache/gstreamer-1.0/registry.*.bin if needed."
        )


def run_pipeline(pipeline):
    def _sigint(signum, frame):
        print("\n[pipeline] Ctrl-C received, sending EOS...")
        pipeline.send_event(Gst.Event.new_eos())

    prev = signal.signal(signal.SIGINT, _sigint)
    bus = pipeline.get_bus()
    print("[pipeline] Compiling models, this may take some time...")
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
                print(f"Error from {msg.src.get_name()}: {err.message}\nDebug: {dbg}")
                break
            if msg.type == Gst.MessageType.EOS:
                print("Pipeline complete.")
                break
    finally:
        signal.signal(signal.SIGINT, prev)
        pipeline.set_state(Gst.State.NULL)


def main():
    args = parse_args()

    input_src = validate_input(args.input)
    detect_model = find_model("**/yolo26m*.xml", "YOLO26m detection")
    vlm_model_dir = MODELS_DIR / "Qwen2.5-VL-3B-Instruct"
    if not vlm_model_dir.exists():
        sys.stderr.write("Error: VLM model not found. Run: python3 export_models.py\n")
        sys.exit(1)

    Path(args.output_video).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output_json).parent.mkdir(parents=True, exist_ok=True)

    detect_device = check_device(args.detect_device, "detection")
    vlm_device = check_device(args.vlm_device, "VLM")

    recheck_ms = args.recheck_interval * 1000

    setup_gst_plugins()

    source_el = build_source(input_src)
    video_stem = Path(input_src).stem
    snapshot_pattern = str(RESULTS_DIR / f"safety_check_{video_stem}-%05d.jpeg")

    pipe = (
        # Source → decode → detect → track
        f'{source_el} ! decodebin3 caps="video/x-raw(ANY)" ! '
        f'gvadetect model="{detect_model}" device={detect_device} '
        f'batch-size=4 threshold={args.threshold} ! queue ! '
        f'gvatrack tracking-type=zero-term-imageless ! '
        f'tee name=t '

        # Branch 1: watermark → encode → save video
        f't. ! queue flush-on-eos=true ! '
        f'gvawatermark name=watermark device=CPU ! '
        f'gvafpscounter ! '
        f'videoconvert ! vah264enc ! h264parse ! '
        f'mp4mux fragment-duration=1000 ! '
        f'filesink location="{args.output_video}" '

        # Branch 2: worker selection → crop → VLM → save snapshots
        f't. ! queue leaky=downstream ! '
        f'gvaworkerselection_py recheck-interval={recheck_ms} ! '
        f'videoconvert ! video/x-raw,format=RGB ! '
        f'gvaworkercrop_py out-width=448 out-height=448 ! '
        f'videoconvertscale ! video/x-raw,width=448,height=448 ! '
        f'gvagenai name=vlm '
        f'model-path="{vlm_model_dir}" '
        f'device={vlm_device} '
        f'prompt-path="{VLM_PROMPT_FILE}" '
        f'generation-config="max_new_tokens=15" '
        f'chunk-size=1 metrics=true ! '
        f'gvawatermark device=CPU ! jpegenc ! '
        f'multifilesink location="{snapshot_pattern}"'
    )

    print(f"\nPipeline:\n{pipe}\n")
    pipeline = Gst.parse_launch(pipe)

    # Set up VLM results tracker and probes
    tracker = VLMResultsTracker(args.output_json, snapshot_pattern)
    pipeline.get_by_name("vlm").get_static_pad("src").add_probe(
        Gst.PadProbeType.BUFFER, _post_vlm_cb, tracker
    )
    pipeline.get_by_name("watermark").get_static_pad("sink").add_probe(
        Gst.PadProbeType.BUFFER, _pre_watermark_cb, tracker
    )

    run_pipeline(pipeline)

    tracker.print_summary()
    print(f"\nOutput video:    {args.output_video}")
    print(f"VLM checks JSON: {args.output_json}")
    print(f"Annotated crops: {RESULTS_DIR}/safety_check_*.jpeg")


if __name__ == "__main__":
    main()
