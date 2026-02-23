# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#!/usr/bin/env python3
"""
Run a DLStreamer VLM pipeline on a video and export JSON and MP4 results.

The script can:
1. Download or reuse a local video.
2. Export or reuse an OpenVINO model.
3. Build a GStreamer pipeline string.
4. Execute the pipeline and store results.
"""

import argparse
import os
import subprocess
import sys
import tempfile
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Tuple

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib # pylint: disable=no-name-in-module, wrong-import-position

BASE_DIR = Path(__file__).resolve().parent
VIDEOS_DIR = BASE_DIR / "videos"
MODELS_DIR = BASE_DIR / "models"
RESULTS_DIR = BASE_DIR / "results"

@dataclass
class PipelineConfig:
    """Configuration required to build and run the pipeline."""

    video: Path
    model: Path
    question: str
    device: str
    max_tokens: int
    frame_rate: float


def ensure_video(path_or_url: str) -> Path:
    """Return a local video path, downloading it if needed."""
    candidate = Path(path_or_url)
    if candidate.is_file():
        return candidate.resolve()

    VIDEOS_DIR.mkdir(exist_ok=True)
    filename = path_or_url.rstrip("/").split("/")[-1]
    local_path = VIDEOS_DIR / filename

    if local_path.exists():
        print(f"[video] using cached {local_path}")
        return local_path.resolve()

    print(f"[video] downloading {path_or_url}")
    request = urllib.request.Request(
        path_or_url,
        headers={"User-Agent": "Mozilla/5.0"},
    )

    with urllib.request.urlopen(request) as response, open(local_path, "wb") as file:
        file.write(response.read())

    return local_path.resolve()


def ensure_model(model_id: str) -> Path:
    """Return a local OpenVINO model directory, exporting it if needed."""
    model_name = model_id.split("/")[-1]
    output_dir = MODELS_DIR / model_name

    if output_dir.exists() and any(output_dir.glob("*.xml")):
        print(f"[model] using cached {output_dir}")
        return output_dir.resolve()

    MODELS_DIR.mkdir(exist_ok=True)

    command = [
        "optimum-cli",
        "export",
        "openvino",
        "--model",
        model_id,
        "--task",
        "image-text-to-text",
        "--trust-remote-code",
        str(output_dir),
    ]

    print("[model] exporting:", " ".join(command))
    subprocess.run(command, check=True)

    if not any(output_dir.glob("*.xml")):
        raise RuntimeError("OpenVINO export failed, no XML files found")

    return output_dir.resolve()


def build_pipeline_string(cfg: PipelineConfig) -> Tuple[str, Path, Path, Path]:
    """Construct the GStreamer pipeline string and related output paths."""
    RESULTS_DIR.mkdir(exist_ok=True)

    output_json = RESULTS_DIR / f"{cfg.model.name}-{cfg.video.stem}.jsonl"
    output_video = RESULTS_DIR / f"{cfg.model.name}-{cfg.video.stem}.mp4"

    fd, prompt_path_str = tempfile.mkstemp(suffix=".txt")
    prompt_path = Path(prompt_path_str)
    with os.fdopen(fd, "w") as file:
        file.write(cfg.question)

    generation_cfg = f"max_new_tokens={cfg.max_tokens}"

    pipeline_str = (
        f'filesrc location="{cfg.video}" ! '
        f'decodebin3 ! '
        f'videoconvertscale ! '
        f'video/x-raw,format=BGRx,width=1280,height=720 ! '
        f'queue ! '
        f'gvagenai '
        f'model-path="{cfg.model}" '
        f'device={cfg.device} '
        f'prompt-path="{prompt_path}" '
        f'generation-config="{generation_cfg}" '
        f'chunk-size=1 '
        f'frame-rate={cfg.frame_rate} '
        f'metrics=true ! '
        f'queue ! '
        f'gvametapublish file-format=json-lines '
        f'file-path="{output_json}" ! '
        f'queue ! '
        f'gvafpscounter ! '
        f'gvawatermark displ-cfg=text-scale=0.5 ! '
        f'videoconvert ! '
        f'vah264enc ! '
        f'h264parse ! '
        f'mp4mux ! '
        f'filesink location="{output_video}"'
    )

    return pipeline_str, output_json, output_video, prompt_path


def run_pipeline_string(pipeline_str: str) -> int:
    """Execute a GStreamer pipeline string and block until completion."""
    Gst.init(None)

    try:
        pipeline = Gst.parse_launch(pipeline_str)
    except GLib.Error as error:
        print("Pipeline parse error:", str(error))
        return 1

    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)

    while True:
        message = bus.timed_pop_filtered(
            Gst.CLOCK_TIME_NONE,
            Gst.MessageType.ERROR | Gst.MessageType.EOS,
        )

        if message.type == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            print("ERROR:", err.message)
            if debug:
                print("DEBUG:", debug)
            pipeline.set_state(Gst.State.NULL)
            return 1

        if message.type == Gst.MessageType.EOS:
            pipeline.set_state(Gst.State.NULL)
            return 0


def run_pipeline(cfg: PipelineConfig) -> int:
    """Build and execute the pipeline from configuration."""
    pipeline_str, output_json, output_video, prompt_path = build_pipeline_string(cfg)

    print("\nPipeline:\n")
    print(pipeline_str)
    print()

    try:
        result = run_pipeline_string(pipeline_str)
    finally:
        if prompt_path.exists():
            prompt_path.unlink()

    if result == 0:
        print(f"\nJSON output:  {output_json}")
        print(f"Video output: {output_video}")

    return result


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="DLStreamer VLM Alerts sample"
    )
    parser.add_argument("video")
    parser.add_argument("model")
    parser.add_argument("question")
    parser.add_argument("--device", default="GPU")
    parser.add_argument("--max-tokens", type=int, default=20)
    parser.add_argument("--frame-rate", type=float, default=1.0)

    return parser.parse_args()


def main() -> int:
    """Entry point."""
    args = parse_args()

    video_path = ensure_video(args.video)
    model_path = ensure_model(args.model)

    config = PipelineConfig(
        video=video_path,
        model=model_path,
        question=args.question,
        device=args.device,
        max_tokens=args.max_tokens,
        frame_rate=args.frame_rate,
    )

    return run_pipeline(config)


if __name__ == "__main__":
    sys.exit(main())
