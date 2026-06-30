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
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Optional
from urllib.parse import urlparse

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstPbutils", "1.0")
from gi.repository import Gst, GLib, GstPbutils  # pylint: disable=no-name-in-module, wrong-import-position

BASE_DIR = Path(__file__).resolve().parent

# Pinned VLM model and revision keep the (otherwise non-deterministic) VLM output
# as reproducible as possible for schema/contract validation.
DEFAULT_MODEL_ID = "OpenGVLab/InternVL3_5-2B"
DEFAULT_MODEL_REVISION = "7d7bd7bcc35b77b391883ebd7f9686cfa0e25cc0"

class VLMAlertsError(Exception):
    """Domain-specific exception for VLM Alerts failures."""

@dataclass
class PipelineConfig:
    video: Path
    model: Path
    prompt: str
    device: str
    max_tokens: int
    num_beams: int
    frame_rate: float
    results_dir: Path
    output_json: Optional[Path] = None


def download_video(url: str, target_path: Path) -> None:
    """Stream an allowlisted HTTPS video URL to ``target_path``."""
    parsed = urlparse(url)
    if parsed.scheme != "https" or parsed.hostname not in {"videos.pexels.com", "www.pexels.com"}:
        raise VLMAlertsError(f"Unexpected video URL: {url}")
    # Some CDNs (e.g. Pexels) reject the default ``Python-urllib`` agent with HTTP 403,
    # so present a common browser User-Agent instead.
    user_agent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
    request = urllib.request.Request(url, headers={"User-Agent": user_agent})
    try:
        with urllib.request.build_opener().open(request, timeout=30) as response, open(target_path, "wb") as file:
            shutil.copyfileobj(response, file)
    except (OSError, ValueError) as error:
        raise VLMAlertsError(f"Video download failed: {error}") from error


def validate_video(video_path: Path) -> None:
    """Raise VLMAlertsError if the file is missing, empty, or not a valid media file."""
    if not video_path.exists() or video_path.stat().st_size == 0:
        raise VLMAlertsError("Video file is missing or empty")

    Gst.init([])
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
    model_revision: Optional[str],
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
    ]
    if model_revision:
        command += ["--revision", model_revision]
    command += [
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


def build_pipeline_string(cfg: PipelineConfig) -> tuple[str, Path, Path, Path]:
    """Construct the GStreamer pipeline string and related output paths."""
    cfg.results_dir.mkdir(parents=True, exist_ok=True)

    if cfg.output_json is not None:
        output_json = cfg.output_json
        output_json.parent.mkdir(parents=True, exist_ok=True)
    else:
        output_json = cfg.results_dir / f"{cfg.model.name}-{cfg.video.stem}.jsonl"
    output_video = cfg.results_dir / f"{cfg.model.name}-{cfg.video.stem}.mp4"

    fd, prompt_path_str = tempfile.mkstemp(suffix=".txt")
    prompt_path = Path(prompt_path_str)

    with os.fdopen(fd, "w") as file:
        file.write(cfg.prompt)

    # for a short yes/no answer (max_new_tokens=1), num_beams=4 is a good default.
    if cfg.num_beams < 1:
        raise VLMAlertsError("num_beams must be >= 1")

    # do_sample=false forces deterministic (greedy / beam-search) decoding so the
    # generated output is as reproducible as possible.
    generation_cfg = f"max_new_tokens={cfg.max_tokens},do_sample=false"
    if cfg.num_beams > 1:
        generation_cfg += f",num_beams={cfg.num_beams}"

    pipeline_str = (
        f'filesrc location="{cfg.video}" ! '
        f'decodebin3 ! '
        f'videoconvertscale ! '
        f'video/x-raw(memory:VAMemory),format=NV12 ! '
        f'queue ! '
        f'gvagenai '
        f'model-path="{cfg.model}" '
        f'device={cfg.device} '
        f'prompt-path="{prompt_path}" '
        f'generation-config="{generation_cfg}" '
        f'chunk-size=1 '
        f'frame-rate={cfg.frame_rate} '
        f'metrics=true ! '
        f'gvametapublish file-format=json-lines '
        f'file-path="{output_json}" ! '
        f'queue ! '
        f'gvafpscounter ! '
        f'gvawatermark name=watermark '
        f'displ-cfg=font-scale=1.5,draw-txt-bg=false,color-idx=1,thickness=5,text-y=680 ! '
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

    Gst.init([])

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

    parser.add_argument("--model-id", default=DEFAULT_MODEL_ID, help="HuggingFace model id")
    parser.add_argument("--model-revision", default=DEFAULT_MODEL_REVISION,
                        help="Pinned HuggingFace model revision (commit SHA) for reproducible export")
    parser.add_argument("--model-path", help="Path to exported OpenVINO model")

    parser.add_argument("--prompt", required=True, help="Text prompt for VLM")

    parser.add_argument("--device", default="GPU")
    parser.add_argument("--max-tokens", type=int, default=1)
    parser.add_argument(
        "--num-beams",
        type=int,
        default=4,
        help=(
            "Number of beams for beam search "
            "(>=2 required for confidence scores; 1 = greedy, no confidence)"
        ),
    )
    parser.add_argument("--frame-rate", type=float, default=1.0)

    parser.add_argument("--videos-dir", type=Path, default=BASE_DIR / "videos")
    parser.add_argument("--models-dir", type=Path, default=BASE_DIR / "models")
    parser.add_argument("--results-dir", type=Path, default=BASE_DIR / "results")
    parser.add_argument("--output-json", type=Path, default=None,
                        help="Override path for the json-lines output (e.g. output.json in the working directory)")

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
        model = resolve_model(args.model_id, args.model_path, args.model_revision, args.models_dir)

        if args.num_beams == 1:
            print(
                "[warning] --num-beams=1 means greedy decoding: "
                "confidence will not be available in output."
            )

        config = PipelineConfig(
            video=video,
            model=model,
            prompt=args.prompt,
            device=args.device,
            max_tokens=args.max_tokens,
            num_beams=args.num_beams,
            frame_rate=args.frame_rate,
            results_dir=args.results_dir,
            output_json=args.output_json,
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
