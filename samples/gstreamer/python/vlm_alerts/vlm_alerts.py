# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#!/usr/bin/env python3
"""
Run a DLStreamer VLM pipeline on a video and export JSON and MP4 results.
"""

import argparse
import os
import subprocess
import sys
import tempfile
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Tuple, Optional

import gi
from gi.repository import Gst, GLib, GstPbutils  # pylint: disable=no-name-in-module
gi.require_version("Gst", "1.0")
gi.require_version("GstPbutils", "1.0")

BASE_DIR = Path(__file__).resolve().parent

class VLMAlertsError(Exception):
    """Domain-specific exception for VLM Alerts failures."""


@dataclass
class PipelineConfig:
    video: Path
    model: Path
    prompt: str
    device: str
    max_tokens: int
    frame_rate: float
    results_dir: Path


def download_video(url: str, target_path: Path) -> None:
    """Return a local video path, downloading it if needed."""
    request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            if hasattr(response, "status") and response.status != 200:
                raise VLMAlertsError(f"Video download failed: HTTP {response.status}")
            data = response.read()
            if not data:
                raise VLMAlertsError("Video download failed: empty response")
            with open(target_path, "wb") as file:
                file.write(data)
    except Exception as error:
        raise VLMAlertsError(f"Video download failed: {error}") from error


def validate_video(video_path: Path) -> None:
    if not video_path.exists() or video_path.stat().st_size == 0:
        raise VLMAlertsError("Video file is missing or empty")

    Gst.init(None)
    try:
        discoverer = GstPbutils.Discoverer.new(5 * Gst.SECOND)
        info = discoverer.discover_uri(video_path.as_uri())
    except GLib.Error as error:
        raise VLMAlertsError(f"GStreamer discovery failed: {error}") from error

    if info.get_result() != GstPbutils.DiscovererResult.OK:
        raise VLMAlertsError(f"Unsupported media: {info.get_result()}")

    if not info.get_stream_list():
        raise VLMAlertsError("No valid streams found in media file")


def resolve_video(
    video_path: Optional[str],
    video_url: Optional[str],
    videos_dir: Path,
) -> Path:
    if video_path:
        path = Path(video_path).resolve()
        if not path.exists():
            raise VLMAlertsError("Provided --video-path does not exist")
        validate_video(path)
        return path

    videos_dir.mkdir(parents=True, exist_ok=True)
    filename = video_url.rstrip("/").split("/")[-1]
    local_path = videos_dir / filename

    if not local_path.exists():
        print(f"[video] downloading {video_url}")
        download_video(video_url, local_path)

    validate_video(local_path)
    return local_path.resolve()


def resolve_model(
    model_id: Optional[str],
    model_path: Optional[str],
    models_dir: Path,
) -> Path:
    """Return a local OpenVINO model directory, exporting it if needed."""
    if model_path:
        path = Path(model_path).resolve()
        if not path.exists():
            raise VLMAlertsError("Provided --model-path does not exist")
        return path

    models_dir.mkdir(parents=True, exist_ok=True)
    model_name = model_id.split("/")[-1]
    output_dir = models_dir / model_name

    if output_dir.exists() and any(output_dir.glob("*.xml")):
        print(f"[model] using cached {output_dir}")
        return output_dir.resolve()

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

    try:
        subprocess.run(command, check=True)
    except subprocess.CalledProcessError as error:
        raise VLMAlertsError(
            f"OpenVINO export failed with return code {error.returncode}"
        ) from error

    if not any(output_dir.glob("*.xml")):
        raise VLMAlertsError("OpenVINO export failed: no XML files found")

    return output_dir.resolve()


def build_pipeline_string(cfg: PipelineConfig) -> Tuple[str, Path, Path, Path]:
    """Construct the GStreamer pipeline string and related output paths."""
    cfg.results_dir.mkdir(parents=True, exist_ok=True)

    output_json = cfg.results_dir / f"{cfg.model.name}-{cfg.video.stem}.jsonl"
    output_video = cfg.results_dir / f"{cfg.model.name}-{cfg.video.stem}.mp4"

    fd, prompt_path_str = tempfile.mkstemp(suffix=".txt")
    prompt_path = Path(prompt_path_str)

    with os.fdopen(fd, "w") as file:
        file.write(cfg.prompt)

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



def run_pipeline(cfg: PipelineConfig) -> int:
    """Execute a GStreamer pipeline string and block until completion."""
    pipeline_str, output_json, output_video, prompt_path = build_pipeline_string(cfg)

    print("\nPipeline:\n")
    print(pipeline_str)
    print()

    Gst.init(None)

    try:
        pipeline = Gst.parse_launch(pipeline_str)
    except GLib.Error as error:
        print("Pipeline parse error:", str(error))
        return 1

    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)

    try:
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
                return 1

            if message.type == Gst.MessageType.EOS:
                break
    finally:
        pipeline.set_state(Gst.State.NULL)
        if prompt_path.exists():
            prompt_path.unlink()

    print(f"\nJSON output:  {output_json}")
    print(f"Video output: {output_video}")

    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="DLStreamer VLM Alerts sample"
    )

    parser.add_argument("--video-path", help="Path to local video file")
    parser.add_argument("--video-url", help="URL to download video from")

    parser.add_argument("--model-id", help="HuggingFace model id")
    parser.add_argument("--model-path", help="Path to exported OpenVINO model")

    parser.add_argument("--prompt", required=True, help="Text prompt for VLM")

    parser.add_argument("--device", default="GPU")
    parser.add_argument("--max-tokens", type=int, default=20)
    parser.add_argument("--frame-rate", type=float, default=1.0)

    parser.add_argument("--videos-dir", type=Path, default=BASE_DIR / "videos")
    parser.add_argument("--models-dir", type=Path, default=BASE_DIR / "models")
    parser.add_argument("--results-dir", type=Path, default=BASE_DIR / "results")

    args = parser.parse_args()

    if not (args.video_path or args.video_url):
        parser.error("Either --video-path or --video-url must be provided")

    if not (args.model_id or args.model_path):
        parser.error("Either --model-id or --model-path must be provided")

    return args


def main() -> int:
    try:
        args = parse_args()

        video = resolve_video(args.video_path, args.video_url, args.videos_dir)
        model = resolve_model(args.model_id, args.model_path, args.models_dir)

        config = PipelineConfig(
            video=video,
            model=model,
            prompt=args.prompt,
            device=args.device,
            max_tokens=args.max_tokens,
            frame_rate=args.frame_rate,
            results_dir=args.results_dir,
        )

        return run_pipeline(config)

    except VLMAlertsError as error:
        print(f"Error: {error}")
        return 1
    except Exception as error:
        print(f"Unexpected failure: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
