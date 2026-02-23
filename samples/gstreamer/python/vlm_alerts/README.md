# VLM Alerts

This sample demonstrates how to download a Vision-Language Model (VLM) from Hugging Face, export it to OpenVINO IR using `optimum-cli`, and run inference in a DL Streamer pipeline.

The pipeline saves both JSON metadata and an encoded MP4 output.

## How It Works

The script performs three main steps:

STEP 1 — Prepare input video  
If a local file is provided, it is used directly.  
If a URL is provided, the video is downloaded automatically into the `videos/` directory.

STEP 2 — Prepare VLM model  

Exported artifacts are stored under:

    models/<ModelName>

STEP 3 — Build and run the pipeline  

The GStreamer pipeline includes:

- gvagenai for VLM inference  
- gvametapublish for JSON output  
- gvafpscounter for performance display  
- gvawatermark for overlay  
- vah264enc for hardware encoding  

The output video and metadata are written to the `results/` directory.

## Setup

From the sample directory:

    cd samples/gstreamer/python/vlm_alerts

Create and activate a virtual environment:

    python3 -m venv .venv --system-site-packages
    source .venv/bin/activate

Install dependencies:

    pip install -r requirements.txt

## Running

    python3 ./vlm_alerts.py <input_video_or_url> <hf_model_id> "<question>"

Example:

    python3 ./vlm_alerts.py \
    https://videos.pexels.com/video-files/2103099/2103099-hd_1280_720_60fps.mp4 \
    OpenGVLab/InternVL3_5-2B \
    "Is there a police car? Answer yes or no."

## Output

After execution:

JSON metadata:

    results/<model>-<video>.jsonl

Annotated video:

    results/<model>-<video>.mp4

## Notes

- Each video and model are downloaded and exported once.
- Different VLMs can be downloaded. Suggested: OpenGVLab/InternVL3_5-2B, openbmb/MiniCPM-V-4_5, Qwen/Qwen2.5-VL-3B-Instruct.
- Subsequent runs reuse cached assets.
- GPU is used by default.
