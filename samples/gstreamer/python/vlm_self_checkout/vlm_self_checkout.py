# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
DLStreamer VLM Self-Checkout classification sample.

Builds a pipeline that:
1. Reads input video stream (from file) and decodes with decodebin3
2. Detects objects using traditional computer vision model (gvadetect)
3. Implements custom frame selection logic in gvaFrameSelection python element
4. Runs VLM model on selected frames for extended object classification 
5. Overlays VLM classification results on top of object detection results in output video frames
6. Saves the annotated video to a file and dumps VLM classification results in a JSONL file
"""

import argparse
import os
import signal
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
gi.require_version("GObject", "2.0")
from gi.repository import Gst, GLib, GObject, GstAnalytics  # pylint: disable=no-name-in-module, wrong-import-position

class GenaiSignalBridge(GObject.Object):
    """
    Cross-branch signal bridge: stores latest frame-selection and VLM results.
    Signal handlers update the bridge's state directly when signals are emitted.
    """

    def __init__(self):
        super().__init__()
        self._frame_selection_quark = None
        self._frame_selection_confidence = 0.0
        self._frame_selection_pts = 0
        self._frame_selection_time = 0
        self._vlm_quark = None
        self._vlm_confidence = 0.0
        self._vlm_pts = 0
        self._vlm_time = 0

    @GObject.Signal(arg_types=(GObject.TYPE_UINT, GObject.TYPE_DOUBLE, GObject.TYPE_UINT64, GObject.TYPE_UINT64))
    def vlm_result(self, label_quark: int, confidence: float, pts: int, system_time_ns: int):
        self._vlm_quark = label_quark
        self._vlm_confidence = confidence
        self._vlm_pts = pts
        self._vlm_time = system_time_ns

    @GObject.Signal(arg_types=(GObject.TYPE_UINT, GObject.TYPE_DOUBLE, GObject.TYPE_UINT64, GObject.TYPE_UINT64))
    def frame_selection(self, objects_quark: int, confidence: float, pts: int, system_time_ns: int):
        self._frame_selection_quark = objects_quark
        self._frame_selection_confidence = confidence
        self._frame_selection_pts = pts
        self._frame_selection_time = system_time_ns

def _post_selection_cb(pad, info, bridge):
    """
    Probe on gvaframeselection_py src pad: extract detected objects and emit frame-selection signal.
    Called for each buffer after frame selection logic.
    """
    buf = info.get_buffer()
    if buf is None:
        return Gst.PadProbeReturn.OK

    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buf)
    if not rmeta:
        return Gst.PadProbeReturn.OK

    labels = []
    confidence = 0.0
    for mtd in rmeta:
        if not isinstance(mtd, GstAnalytics.ODMtd):
            continue
        label = GLib.quark_to_string(mtd.get_obj_type())
        if label:
            labels.append(label)
            _, confidence_lvl = mtd.get_confidence_lvl()
            confidence = max(confidence, confidence_lvl)

    if labels:
        objects_quark = GLib.quark_from_string(", ".join(labels))
        bridge.emit("frame-selection", objects_quark, confidence, int(buf.pts), int(time.time_ns()))

    return Gst.PadProbeReturn.OK

def _post_vlm_cb(pad, info, bridge):
    """
    Probe on gvagenai src pad: extract VLM result and emit signal.
    Called for each buffer after VLM model inference.
    """
    buf = info.get_buffer()
    if buf is None:
        return Gst.PadProbeReturn.OK

    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buf)
    if not rmeta:
        return Gst.PadProbeReturn.OK

    # retrieve analysis result from VLM model and emit it via the bridge
    for mtd in rmeta:
        if isinstance(mtd, GstAnalytics.ClsMtd) and mtd.get_quark(0):
            bridge.emit("vlm-result", int(mtd.get_quark(0)), float(mtd.get_level(0)), int(buf.pts), int(time.time_ns()))
            break

    return Gst.PadProbeReturn.OK


def _pre_watermark_cb(pad, info, bridge):
    """
    Probe on gvawatermark sink pad: overlay information from frame-selection and VLM results.
    Adds overlay metadata to the frame for display.
    """
    buf = info.get_buffer()
    if buf is None:
        return Gst.PadProbeReturn.OK

    # get (or create) analytics metadata attached to a frame buffer
    rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buf)
    if not rmeta:
        if buf.make_writable():
            rmeta = GstAnalytics.buffer_add_analytics_relation_meta(buf)
        if not rmeta:
            print("[probes] failed to add analytics metadata to buffer")
            return Gst.PadProbeReturn.OK

    # display frame selection output for max 4 seconds
    if bridge._frame_selection_quark is not None and (buf.pts - bridge._frame_selection_pts) < 4 * Gst.SECOND:
        frame_time = bridge._frame_selection_pts / Gst.SECOND
        text = f"[{frame_time:.2f} s] Frame selection, detected objects: {GLib.quark_to_string(bridge._frame_selection_quark)} "
        rmeta.add_od_mtd(GLib.quark_from_string(text), 10, 50, 0, 0, bridge._frame_selection_confidence)

        # display VLM classification output for most recently selected frame
        if (bridge._vlm_quark is not None) and (bridge._vlm_pts >= bridge._frame_selection_pts):
            vlm_time = frame_time + (bridge._vlm_time - bridge._frame_selection_time) / 1e9
            text = f"[{vlm_time:.2f} s] VLM classification: {GLib.quark_to_string(bridge._vlm_quark)}, confidence:"
            rmeta.add_od_mtd(GLib.quark_from_string(text), 10, 100, 0, 0, bridge._vlm_confidence)

    return Gst.PadProbeReturn.OK


BASE_DIR = Path(__file__).resolve().parent
CONFIG_DIR = BASE_DIR / "config"
VIDEOS_DIR = BASE_DIR / "videos"
MODELS_DIR = BASE_DIR / "models"
RESULTS_DIR = BASE_DIR / "results"
DEFAULT_VIDEO_URL = (
    "https://www.pexels.com/download/video/35256160"
)

def download_video(video_url: str) -> Path:
    """Return a local video path, downloading from URL if needed."""
    VIDEOS_DIR.mkdir(parents=True, exist_ok=True)
    filename = video_url.rstrip("/").split("/")[-1]
    if not Path(filename).suffix:
        filename += ".mp4"

    local_path = VIDEOS_DIR / filename
    if not local_path.exists():
        request = urllib.request.Request(video_url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read()
            if not data:
                raise RuntimeError("Video download returned empty response")
            with open(local_path, "wb") as fh:
                fh.write(data)

    return local_path.resolve()


DOWNLOAD_SCRIPT = Path(__file__).resolve().parents[4] / "scripts" / "download_models" / "download_ultralytics_models.py"

def download_detection_model(model_id: str) -> Path:
    """Return a path to the local file with YOLO OpenVINO IR model.
    Spawn a separate process, as Ultralytics export will create a new instance of OpenVINO runtime
    which may clash with OpenVINO runtime instance used by DLStreamer."""
    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    model_path = MODELS_DIR / f"{model_id}_int8_openvino_model" / f"{model_id}.xml"

    if not model_path.exists():
        print(f"[detect] exporting {model_id} to OpenVINO format (subprocess)")
        pt_file = BASE_DIR / f"{model_id}.pt"
        result = subprocess.run(
            [sys.executable, str(DOWNLOAD_SCRIPT),
             "--model", str(pt_file),
             "--outdir", str(MODELS_DIR),
             "--int8"],
            check=False
        )
        if result.returncode != 0:
            raise RuntimeError(f"YOLO model export failed with exit code {result.returncode}")
        if not model_path.exists():
            raise RuntimeError(f"Expected model not found at {model_path} after export")

    return model_path.resolve()


def download_vlm_model(model_id: str) -> Path:
    """Return a path to the VLM OpenVINO model, downloading/exporting via a optimum-cli (separate process."""
    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    model_name = model_id.split("/")[-1]
    model_path = MODELS_DIR / model_name

    if not model_path.exists():
        print(f"[vlm] downloading and exporting {model_id} to OpenVINO format")
        command = [
            "optimum-cli",
            "export",
            "openvino",
            "--model",
            model_id,
            "--task",
            "image-text-to-text",
            "--trust-remote-code",
            str(model_path),
        ]
        result = subprocess.run(command, check=False)
        if result.returncode != 0:
            raise RuntimeError(f"VLM model export failed with exit code {result.returncode}")
        if not model_path.exists():
            raise RuntimeError(f"Expected VLM model not found at {model_path} after export")

    return model_path.resolve()


def construct_pipeline(
    video_file: Path,
    model_file: Path,
    detect_device: str,
    threshold: float,
    genai_model: Path,
    genai_device: str,
    genai_prompt: str,
    inventory_file: Path,
    excluded_objects_file: Path,
) -> Gst.Pipeline:
    """Construct the GStreamer pipeline, attach probes, and return it."""
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    output_video = RESULTS_DIR / f"vlm_self_checkout-{video_file.stem}.mp4"
    output_files = RESULTS_DIR / f"vlm_self_checkout-{video_file.stem}-%d.jpeg"
    output_json = RESULTS_DIR / f"vlm_self_checkout-{video_file.stem}.jsonl"

    pipeline_str = (
        # Source → decode → detect
        f'filesrc location="{video_file}" ! '
        f'decodebin3 ! '
        f'gvafpsthrottle target-fps=30 ! '
        f'gvadetect model="{model_file}" '
        f'device={detect_device} '
        f'threshold={threshold} ! '
        f'queue ! '
        f'gvatrack tracking-type=zero-term-imageless ! '
        f'tee name=detect_tee '

        # Path 1: encode and store annotated video stream in a file
        f'detect_tee. ! '
        f'queue ! '
        f'gvawatermark name=watermark device=CPU displ-cfg=font-scale=1.0 ! '
        f'gvafpscounter ! '
        f'vah264enc ! '
        f'h264parse ! '
        f'mp4mux ! '
        f'filesink location="{output_video}" '

        # Path 2: analytics — frame selection filter → VLM classification → save snapshots
        f'detect_tee. ! '
        f'queue ! '
        f'gvaframeselection_py name=selection threshold=1500 genai-name=vlm inventory-file="{inventory_file}" excluded-objects-file="{excluded_objects_file}" ! '
        f'vapostproc ! video/x-raw,format=NV12,width=640,height=360 ! '
        f'gvagenai name=vlm '
        f'model-path="{genai_model}" '
        f'device={genai_device} '
        f'prompt="{genai_prompt}" '
        f'generation-config="max_new_tokens=50" '
        f'chunk-size=1 '
        f'metrics=true ! '
        f'gvametapublish name=metapublish file-format=json-lines '
        f'file-path="{output_json}" ! '
        f'jpegenc ! '
        f'multifilesink location="{output_files}"'
    )

    print(f"[construct_pipeline] Pipeline: {pipeline_str}")

    try:
        pipeline = Gst.parse_launch(pipeline_str)
    except GLib.Error as error:
        raise RuntimeError(f"Pipeline parse error: {error}") from error

    # --- Set up cross-branch analytics signal bridge ---
    # gvaframeselection and gvagenai output (src) probes will generate signals consumed by watermark input (sink) probe
    # no need to check if elements exist, as they are created just above and parse_launch would have failed if they were missing
    bridge = GenaiSignalBridge()
    pipeline.get_by_name("selection").get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, _post_selection_cb, bridge)
    pipeline.get_by_name("vlm").get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, _post_vlm_cb, bridge)
    pipeline.get_by_name("watermark").get_static_pad("sink").add_probe(Gst.PadProbeType.BUFFER, _pre_watermark_cb, bridge)

    return pipeline

def setup_gst_plugins() -> None:
    """Register local Python plugin path and initialise GStreamer."""
    plugins_dir = str(BASE_DIR / "plugins")
    if plugins_dir not in os.environ.get("GST_PLUGIN_PATH", ""):
        print(f'[plugins] adding "{plugins_dir}" to GST_PLUGIN_PATH')
        existing_path = os.environ.get("GST_PLUGIN_PATH", "")
        os.environ["GST_PLUGIN_PATH"] = (
            f"{existing_path}:{plugins_dir}" if existing_path else plugins_dir
        )

    Gst.init(None)

    reg = Gst.Registry.get()
    if not reg.find_plugin("python"):
        raise RuntimeError(
            "GStreamer 'python' plugin not found. "
            "Ensure GST_PLUGIN_PATH includes the path to libgstpython.so. "
            "If the error persists, delete the GStreamer registry cache: "
            "rm ~/.cache/gstreamer-1.0/registry.x86_64.bin"
        )

def run_pipeline(pipeline: Gst.Pipeline) -> None:
    """Run a GStreamer pipeline and handle user interrupt (Ctrl-C)."""

    # Handle Ctrl-C: send EOS so muxers finalize properly
    def _sigint_handler(signum, frame):
        print("\n[pipeline] Ctrl-C received, sending EOS...")
        pipeline.send_event(Gst.Event.new_eos())
    prev_handler = signal.signal(signal.SIGINT, _sigint_handler)

    print("[pipeline] Starting (compiling models)...")
    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)
    ret = pipeline.get_state(Gst.CLOCK_TIME_NONE)
    if ret[0] != Gst.StateChangeReturn.SUCCESS:
        raise RuntimeError(f"Pipeline failed to reach PLAYING state: {ret}")
    
    print("[pipeline] Running... Press Ctrl-C to stop.")
    try:
        while True:
            message = bus.timed_pop_filtered(
                100 * Gst.MSECOND,
                Gst.MessageType.ERROR | Gst.MessageType.EOS,
            )
            if message is None:
                continue            
            if message.type == Gst.MessageType.ERROR:
                err, debug = message.parse_error()
                raise RuntimeError(f"Pipeline error: {err.message}\nDebug: {debug}")
            if message.type == Gst.MessageType.EOS:
                print("[pipeline] EOS received, shutting down")
                break
    finally:
        signal.signal(signal.SIGINT, prev_handler)
        pipeline.set_state(Gst.State.NULL)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="DLStreamer VLM Self Checkout sample")

    parser.add_argument("--video-url", default=DEFAULT_VIDEO_URL,
                        help="URL to download a video from (used when --video-path is omitted)")

    parser.add_argument("--detect-model-id", default="yolo26s", help="Ultralytics model id")
    parser.add_argument("--detect-device", default="GPU", help="Device for YOLO detection")
    parser.add_argument("--threshold", type=float, default=0.4,
                        help="Detection confidence threshold")
    parser.add_argument("--inventory-file", default=str(CONFIG_DIR / "inventory.txt"),
                        help="Path to text file listing inventory items, one per line")
    parser.add_argument("--excluded-objects-file", default=str(CONFIG_DIR / "excluded_objects.txt"),
                        help="Path to text file listing excluded object types, one per line")
    parser.add_argument("--vlm-model-id", default="openbmb/MiniCPM-V-4_5",
                        help="Hugging Face model id for VLM (will be downloaded and exported to OpenVINO)")
    parser.add_argument("--genai-device", default="GPU", help="Device for gvagenai inference")
    parser.add_argument("--genai-prompt",
                        default="Describe the items visible on the self-checkout counter.",
                        help="Initial prompt for gvagenai VLM inference")

    return parser.parse_args()

def main() -> int:
    args = parse_args()

    video_file = download_video(args.video_url)
    detection_model = download_detection_model(args.detect_model_id)
    genai_model = download_vlm_model(args.vlm_model_id)
    inventory_file = Path(args.inventory_file).resolve()
    excluded_objects_file = Path(args.excluded_objects_file).resolve()

    setup_gst_plugins()
    pipeline = construct_pipeline(
        video_file, detection_model, args.detect_device, args.threshold,
        genai_model, args.genai_device, args.genai_prompt, inventory_file, excluded_objects_file)
    run_pipeline(pipeline)

    return 0

if __name__ == "__main__":
    sys.exit(main())
