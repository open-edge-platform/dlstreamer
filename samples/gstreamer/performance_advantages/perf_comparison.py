#!/usr/bin/env python3
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""DL Streamer vs OpenCV + OpenVINO performance comparison."""

import argparse
import statistics
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np
import openvino as ov

SCRIPT_DIR = Path(__file__).resolve().parent
MODEL_NAME = "yolo26s"
MODEL_DIR = SCRIPT_DIR / f"{MODEL_NAME}_int8_openvino_model"
MODEL_XML = MODEL_DIR / f"{MODEL_NAME}.xml"
OUTPUT_DIR = SCRIPT_DIR / "output"

VIDEO_URL = ("https://storage.openvinotoolkit.org/repositories/"
             "openvino_notebooks/data/data/video/people.mp4")
VIDEO_PATH = SCRIPT_DIR / "data" / "people.mp4"

YOLO_INPUT_SIZE = 640               # YOLO26s fixed input resolution
CONFIDENCE_THRESHOLD = 0.35
LETTERBOX_PAD_VALUE = 114            # standard YOLO gray padding
INFERENCE_DEVICE = "GPU"
NIREQ = 4                           # async slots for DLStreamer pipeline
QUEUE_SIZE = 8                       # GStreamer queue depth
SNAPSHOT_FRAMES = 90                 # frames for watermarked snapshot
RUN_COOLDOWN = 2                     # seconds between runs (thermal)

COLORS = [
    (56, 56, 255), (151, 157, 255), (31, 112, 255), (29, 178, 255),
    (49, 210, 207), (10, 249, 72), (23, 204, 146), (134, 219, 61),
    (182, 219, 61), (221, 111, 76),
]


class PipelineError(RuntimeError):
    """Raised when a pipeline stage encounters an unrecoverable condition."""


def prepare_model() -> Path:
    """Export YOLO26s to OpenVINO INT8 if not cached."""
    if MODEL_XML.exists():
        return MODEL_XML
    from ultralytics import YOLO  # pylint: disable=import-outside-toplevel
    print(f"Exporting {MODEL_NAME} to OpenVINO INT8 ...")
    out = Path(YOLO(MODEL_NAME).export(
        format="openvino", int8=True, imgsz=YOLO_INPUT_SIZE, dynamic=False))
    if out.resolve() != MODEL_DIR.resolve():
        MODEL_DIR.mkdir(parents=True, exist_ok=True)
        for f in out.iterdir():
            f.rename(MODEL_DIR / f.name)
    return MODEL_XML


def prepare_video() -> Path:
    """Download test video if not cached."""
    if VIDEO_PATH.exists():
        return VIDEO_PATH
    VIDEO_PATH.parent.mkdir(parents=True, exist_ok=True)
    print("Downloading test video ...")
    urllib.request.urlretrieve(VIDEO_URL, VIDEO_PATH)
    return VIDEO_PATH


def load_class_names() -> dict[int, str]:
    """Parse COCO class names from ultralytics metadata."""
    meta = MODEL_DIR / "metadata.yaml"
    if not meta.exists():
        return {}
    names: dict[int, str] = {}
    for line in meta.read_text().splitlines():
        stripped = line.strip()
        if stripped and stripped[0].isdigit() and ":" in stripped:
            key, val = stripped.split(":", 1)
            names[int(key.strip())] = val.strip()
    return names


@dataclass(frozen=True, slots=True)
class Result:
    """Per-run measurement data."""
    fps: float
    e2e_ms: float
    infer_ms: float
    frames: int
    detections: int = 0

    def __str__(self) -> str:
        det = f", {self.detections} det" if self.detections else ""
        return (f"{self.fps:.1f} fps  e2e={self.e2e_ms:.1f} ms"
                f"  infer={self.infer_ms:.1f} ms"
                f"  ({self.frames} frames{det})")


def letterbox(frame: np.ndarray) -> tuple[np.ndarray, float, int, int]:
    """Aspect-preserving resize with padding; returns NCHW blob, scale, pad_x, pad_y."""
    h, w = frame.shape[:2]
    scale = min(YOLO_INPUT_SIZE / h, YOLO_INPUT_SIZE / w)
    new_h, new_w = int(h * scale), int(w * scale)
    resized = cv2.resize(frame, (new_w, new_h))
    canvas = np.full((YOLO_INPUT_SIZE, YOLO_INPUT_SIZE, 3),
                     LETTERBOX_PAD_VALUE, dtype=np.uint8)
    pad_y = (YOLO_INPUT_SIZE - new_h) // 2
    pad_x = (YOLO_INPUT_SIZE - new_w) // 2
    canvas[pad_y:pad_y + new_h, pad_x:pad_x + new_w] = resized
    blob = (canvas.astype(np.float32) / 255.0).transpose(2, 0, 1)[np.newaxis]
    return blob, scale, pad_x, pad_y


def decode_detections(
    output: np.ndarray, scale: float, pad_x: int, pad_y: int,
) -> list[tuple[int, int, int, int, float, int]]:
    """Map YOLO26 output [1,300,6] back to original image coordinates."""
    preds = output.squeeze(0)
    return [
        (int((d[0] - pad_x) / scale), int((d[1] - pad_y) / scale),
         int((d[2] - pad_x) / scale), int((d[3] - pad_y) / scale),
         float(d[4]), int(d[5]))
        for d in preds if float(d[4]) >= CONFIDENCE_THRESHOLD
    ]


def draw_boxes(
    frame: np.ndarray,
    dets: list[tuple[int, int, int, int, float, int]],
    names: dict[int, str],
) -> np.ndarray:
    """Draw bounding boxes with class labels."""
    vis = frame.copy()
    h, w = vis.shape[:2]
    for x1, y1, x2, y2, score, cls in dets:
        x1, y1, x2, y2 = max(0, x1), max(0, y1), min(w, x2), min(h, y2)
        color = COLORS[cls % len(COLORS)]
        cv2.rectangle(vis, (x1, y1), (x2, y2), color, 2)
        label = f"{names.get(cls, str(cls))} {score:.0%}"
        (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.55, 1)
        label_y = max(y1, th + 8)
        cv2.rectangle(vis, (x1, label_y - th - 6), (x1 + tw + 4, label_y), color, -1)
        cv2.putText(vis, label, (x1 + 2, label_y - 4),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 1, cv2.LINE_AA)
    return vis


def stamp_frame(frame: np.ndarray, text: str) -> np.ndarray:
    """Semi-transparent timing banner at the top."""
    overlay = frame.copy()
    (tw, th), _ = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.7, 2)
    cv2.rectangle(overlay, (8, 6), (tw + 16, th + 16), (0, 0, 0), -1)
    cv2.addWeighted(overlay, 0.5, frame, 0.5, 0, frame)
    cv2.putText(frame, text, (12, th + 10),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2, cv2.LINE_AA)
    return frame


def read_frame(cap: cv2.VideoCapture) -> np.ndarray:
    """Read next frame, loop if video ends."""
    ok, frame = cap.read()
    if not ok:
        cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
        ok, frame = cap.read()
        if not ok or frame is None:
            raise PipelineError("Cannot read frame from video")
    return frame


# -- traditional pipeline ----------------------------------------------------

def run_traditional(
    model_path: Path, video: Path, num_frames: int, warmup: int,
    save_path: Path | None = None, names: dict[int, str] | None = None,
) -> Result:
    """Synchronous OpenCV decode + OpenVINO iGPU inference loop."""
    core = ov.Core()
    if INFERENCE_DEVICE not in core.available_devices:
        raise PipelineError(
            f"{INFERENCE_DEVICE} not available, found: {core.available_devices}")
    compiled = core.compile_model(core.read_model(str(model_path)), INFERENCE_DEVICE)
    req = compiled.create_infer_request()
    cap = cv2.VideoCapture(str(video))
    if not cap.isOpened():
        raise PipelineError(f"Cannot open video: {video}")

    for _ in range(warmup):
        req.infer({0: letterbox(read_frame(cap))[0]})

    cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
    infer_times: list[float] = []
    wall_times: list[float] = []
    total_dets = 0
    saved = False

    for _ in range(num_frames):
        frame = read_frame(cap)
        blob, scale, pad_x, pad_y = letterbox(frame)
        t0 = time.monotonic()
        req.infer({0: blob})
        infer_times.append((time.monotonic() - t0) * 1000.0)
        wall_times.append(time.monotonic())
        dets = decode_detections(req.get_output_tensor(0).data, scale, pad_x, pad_y)
        total_dets += len(dets)
        if save_path and not saved and dets:
            vis = draw_boxes(frame, dets, names or {})
            stamp_frame(vis, f"E2E via OpenCV + OpenVINO: {infer_times[-1]:.1f} ms")
            cv2.imwrite(str(save_path), vis)
            saved = True

    cap.release()
    if len(wall_times) < 2:
        raise PipelineError("Too few frames measured in traditional pipeline")
    elapsed = wall_times[-1] - wall_times[0]
    fps = (len(wall_times) - 1) / elapsed if elapsed > 0 else 0
    e2e_diffs = [(wall_times[i] - wall_times[i - 1]) * 1000.0 for i in range(1, len(wall_times))]
    return Result(fps=fps, e2e_ms=statistics.mean(e2e_diffs),
                  infer_ms=statistics.mean(infer_times),
                  frames=len(wall_times), detections=total_dets)


# -- DLStreamer pipeline ------------------------------------------------------

def save_dls_snapshot(
    model_xml: Path, video: Path, out_path: Path, e2e_ms: float,
) -> None:
    """Save one watermarked detection frame via DLStreamer."""
    import gi  # pylint: disable=import-outside-toplevel
    gi.require_version("Gst", "1.0")
    from gi.repository import Gst  # pylint: disable=import-outside-toplevel
    Gst.init(None)

    # vapostproc needed for gvawatermark to render on VA surface
    pipe = Gst.parse_launch(
        f"filesrc location={video} num-buffers={SNAPSHOT_FRAMES} "
        f"! qtdemux ! h264parse ! vah264dec ! vapostproc "
        f"! video/x-raw(memory:VAMemory),format=NV12 "
        f"! gvadetect model={model_xml} device={INFERENCE_DEVICE} "
        f"pre-process-backend=va-surface-sharing nireq={NIREQ} batch-size=1 "
        f"threshold={CONFIDENCE_THRESHOLD} "
        f"! queue ! vapostproc ! gvawatermark ! videoconvert "
        f"! jpegenc snapshot=true ! filesink location={out_path}")
    pipe.set_state(Gst.State.PLAYING)
    pipe.get_bus().timed_pop_filtered(
        30 * Gst.SECOND, Gst.MessageType.EOS | Gst.MessageType.ERROR)
    pipe.set_state(Gst.State.NULL)

    if out_path.exists():
        img = cv2.imread(str(out_path))
        if img is not None:
            stamp_frame(img, f"E2E via DLStreamer: {e2e_ms:.1f} ms")
            cv2.imwrite(str(out_path), img)


def run_dlstreamer(
    model_xml: Path, video: Path, num_frames: int, warmup: int,
) -> Result:
    """Pipelined iGPU decode + zero-copy inference via DLStreamer."""
    import gi  # pylint: disable=import-outside-toplevel
    gi.require_version("Gst", "1.0")
    from gi.repository import Gst  # pylint: disable=import-outside-toplevel
    Gst.init(None)

    pipeline_str = (
        f"filesrc location={video} "
        f"! qtdemux ! h264parse "
        f"! vah264dec "
        f"! video/x-raw(memory:VAMemory),format=NV12 "
        f"! gvadetect model={model_xml} device={INFERENCE_DEVICE} "
        f"pre-process-backend=va-surface-sharing nireq={NIREQ} batch-size=1 "
        f"threshold={CONFIDENCE_THRESHOLD} "
        f"! queue max-size-buffers={QUEUE_SIZE} "
        f"! identity name=tap "
        f"! fakesink async=false sync=false")

    total = warmup + num_frames
    timestamps: list[float] = []
    counter = [0]

    def on_buffer(pad, _info, _user_data):
        counter[0] += 1
        if counter[0] > warmup:
            timestamps.append(time.monotonic())
        if counter[0] >= total:
            pad.get_parent_element().get_parent().send_event(Gst.Event.new_eos())
        return Gst.PadProbeReturn.OK

    pipeline = Gst.parse_launch(pipeline_str)
    tap = pipeline.get_by_name("tap")
    if tap is None:
        raise PipelineError("Failed to construct DLStreamer pipeline")
    tap.get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, on_buffer, None)

    pipeline.set_state(Gst.State.PLAYING)
    bus = pipeline.get_bus()
    while True:
        msg = bus.timed_pop_filtered(
            Gst.CLOCK_TIME_NONE, Gst.MessageType.EOS | Gst.MessageType.ERROR)
        if not msg:
            continue
        if msg.type == Gst.MessageType.ERROR:
            err, dbg = msg.parse_error()
            raise PipelineError(f"GStreamer error: {err.message}\n{dbg}")
        break
    pipeline.set_state(Gst.State.NULL)

    if len(timestamps) < 2:
        raise PipelineError("Too few frames measured in DLStreamer pipeline")
    elapsed = timestamps[-1] - timestamps[0]
    count = len(timestamps)
    fps = (count - 1) / elapsed if elapsed > 0 else 0
    inter_frame = [(timestamps[i] - timestamps[i - 1]) * 1000.0 for i in range(1, count)]
    e2e = statistics.mean(inter_frame)
    return Result(fps=fps, e2e_ms=e2e, infer_ms=e2e, frames=count)


# -- main ---------------------------------------------------------------------

def main() -> None:
    """Run both pipelines and print comparison."""
    parser = argparse.ArgumentParser(
        description="DL Streamer vs OpenCV + OpenVINO performance comparison",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--video", type=Path, default=None)
    parser.add_argument("--model", type=Path, default=None)
    parser.add_argument("--frames", type=int, default=200)
    parser.add_argument("--warmup", type=int, default=50)
    parser.add_argument("--runs", type=int, default=3)
    args = parser.parse_args()

    model_xml = args.model or prepare_model()
    video = args.video or prepare_video()
    if not model_xml.exists():
        raise FileNotFoundError(f"Model not found: {model_xml}")
    if not video.exists():
        raise FileNotFoundError(f"Video not found: {video}")
    names = load_class_names()
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    print(f"Model : {model_xml}")
    print(f"Video : {video}")
    print(f"Config: {args.frames} frames + {args.warmup} warmup x {args.runs} runs\n")

    trad_img = OUTPUT_DIR / "traditional_detection.jpg"
    print("Traditional pipeline (OpenCV decode, OpenVINO iGPU inference)")
    trad_results: list[Result] = []
    for i in range(args.runs):
        result = run_traditional(
            model_xml, video, args.frames, args.warmup,
            save_path=trad_img if i == 0 else None, names=names)
        trad_results.append(result)
        print(f"  run {i + 1}: {result}")
        time.sleep(RUN_COOLDOWN)

    print("\nDLStreamer pipeline (iGPU decode, zero-copy, async inference)")
    dls_results: list[Result] = []
    for i in range(args.runs):
        result = run_dlstreamer(model_xml, video, args.frames, args.warmup)
        dls_results.append(result)
        print(f"  run {i + 1}: {result}")
        time.sleep(RUN_COOLDOWN)

    t_fps = statistics.mean(r.fps for r in trad_results)
    d_fps = statistics.mean(r.fps for r in dls_results)
    t_e2e = statistics.mean(r.e2e_ms for r in trad_results)
    d_e2e = statistics.mean(r.e2e_ms for r in dls_results)
    t_inf = statistics.mean(r.infer_ms for r in trad_results)
    d_inf = statistics.mean(r.infer_ms for r in dls_results)

    dls_img = OUTPUT_DIR / "dlstreamer_detection.jpg"
    save_dls_snapshot(model_xml, video, dls_img, e2e_ms=d_e2e)

    throughput_pct = (d_fps - t_fps) / t_fps * 100 if t_fps else 0
    latency_pct = (t_e2e - d_e2e) / t_e2e * 100 if t_e2e else 0

    sep = "-" * 64
    print(f"\n{sep}")
    print(f"  Traditional : {t_fps:>7.1f} fps   e2e = {t_e2e:.1f} ms   infer = {t_inf:.1f} ms")
    print(f"  DLStreamer   : {d_fps:>7.1f} fps   e2e = {d_e2e:.1f} ms   infer = {d_inf:.1f} ms")
    print(sep)
    print("  DLStreamer advantage on ARL/PTL:")
    print(f"  Up to {throughput_pct:.0f}% higher throughput, {latency_pct:.0f}% lower e2e latency")
    print(f"\n  Detection output: {OUTPUT_DIR}")
    print(sep)


if __name__ == "__main__":
    main()
