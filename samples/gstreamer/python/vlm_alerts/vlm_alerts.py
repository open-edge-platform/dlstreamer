# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#!/usr/bin/env python3

import argparse
import os
import sys
import subprocess
import urllib.request
import tempfile
from pathlib import Path

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst

BASE_DIR = Path(__file__).resolve().parent
VIDEOS_DIR = BASE_DIR / "videos"
MODELS_DIR = BASE_DIR / "models"
RESULTS_DIR = BASE_DIR / "results"

def ensure_video(path_or_url: str) -> Path:
    p = Path(path_or_url)
    if p.is_file():
        return p.resolve()

    VIDEOS_DIR.mkdir(exist_ok=True)
    filename = path_or_url.rstrip("/").split("/")[-1]
    local_path = VIDEOS_DIR / filename

    if local_path.exists():
        print(f"[video] using cached {local_path}")
        return local_path

    print(f"[video] downloading {path_or_url}")
    req = urllib.request.Request(
        path_or_url,
        headers={"User-Agent": "Mozilla/5.0"},
    )
    with urllib.request.urlopen(req) as r, open(local_path, "wb") as f:
        f.write(r.read())

    return local_path.resolve()

def ensure_model(model_id: str) -> Path:
    model_name = model_id.split("/")[-1]
    output_dir = MODELS_DIR / model_name

    if output_dir.exists() and any(output_dir.glob("*.xml")):
        print(f"[model] using cached {output_dir}")
        return output_dir.resolve()

    MODELS_DIR.mkdir(exist_ok=True)

    cmd = [
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

    print("[model] exporting:", " ".join(cmd))
    subprocess.run(cmd, check=True)

    if not any(output_dir.glob("*.xml")):
        raise RuntimeError("OpenVINO export failed: no XML files found")

    return output_dir.resolve()

def pipeline_loop(pipeline: Gst.Element):
    bus = pipeline.get_bus()
    pipeline.set_state(Gst.State.PLAYING)

    while True:
        msg = bus.timed_pop_filtered(
            Gst.CLOCK_TIME_NONE,
            Gst.MessageType.ERROR | Gst.MessageType.EOS,
        )

        if msg.type == Gst.MessageType.ERROR:
            err, dbg = msg.parse_error()
            print("ERROR:", err.message)
            print("DEBUG:", dbg)
            break

        if msg.type == Gst.MessageType.EOS:
            print("Pipeline complete")
            break

    pipeline.set_state(Gst.State.NULL)


def run_pipeline(video: Path,
                 model: Path,
                 question: str,
                 device: str,
                 max_tokens: int,
                 frame_rate: float):

    RESULTS_DIR.mkdir(exist_ok=True)

    output_json = RESULTS_DIR / f"{model.name}-{video.stem}.jsonl"
    output_video = RESULTS_DIR / f"{model.name}-{video.stem}.mp4"

    fd, prompt_path = tempfile.mkstemp(suffix=".txt")
    with os.fdopen(fd, "w") as f:
        f.write(question)

    generation_cfg = f"max_new_tokens={max_tokens}"

    pipeline_str = (
        f'filesrc location="{video}" ! '
        f'decodebin3 ! '
        f'videoconvertscale ! '
        f'video/x-raw,format=BGRx,width=1280,height=720 ! '
        f'queue ! '
        f'gvagenai '
        f'model-path="{model}" '
        f'device={device} '
        f'prompt-path="{prompt_path}" '
        f'generation-config="{generation_cfg}" '
        f'chunk-size=1 '
        f'frame-rate={frame_rate} '
        f'metrics=true '
        f'! queue ! '
        f'gvawatermark '
        f'! queue ! '
        f'gvametapublish file-format=json-lines '
        f'file-path="{output_json}" '
        f'! queue ! '
        f'videoconvert '
        f'! vah264enc '
        f'! h264parse '
        f'! mp4mux '
        f'! filesink location="{output_video}"'
    )

    print("\nPipeline:\n")
    print(pipeline_str)
    print()

    Gst.init(None)

    try:
        pipeline = Gst.parse_launch(pipeline_str)
    except Exception as e:
        print("Pipeline parse error:", e)
        os.unlink(prompt_path)
        return 1

    pipeline_loop(pipeline)

    os.unlink(prompt_path)

    print(f"\nJSON output:  {output_json}")
    print(f"Video output: {output_video}")
    return 0

def main():
    parser = argparse.ArgumentParser(
        description="DLStreamer VLM Alerts sample"
    )
    parser.add_argument("video")
    parser.add_argument("model")
    parser.add_argument("question")
    parser.add_argument("--device", default="GPU")
    parser.add_argument("--max-tokens", type=int, default=20)
    parser.add_argument("--frame-rate", type=float, default=1.0)

    args = parser.parse_args()

    video_path = ensure_video(args.video)
    model_path = ensure_model(args.model)

    return run_pipeline(
        video=video_path,
        model=model_path,
        question=args.question,
        device=args.device,
        max_tokens=args.max_tokens,
        frame_rate=args.frame_rate,
    )


if __name__ == "__main__":
    sys.exit(main())
