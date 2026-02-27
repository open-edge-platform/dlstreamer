# VLM Alerts

This sample demonstrates an edge AI alerting pipeline using Vision-Language Models (VLMs).

It shows how to:

- Download a VLM from Hugging Face
- Convert it to OpenVINO IR using `optimum-cli`
- Run inference inside a DL Streamer pipeline
- Generate structured JSON alerts per processed frame
- Produce MP4 output

## Use Case: Alert-Based Monitoring

VLMs can help accurately detect rare or contextual events using natural language prompts — for example, detecting a police car in a traffic video.
This enables alerting for events, like in prompts:

- Is there a police car?
- Is there smoke or fire?
- Is a person lying on the ground?

## Model Preparation

Any image-text-to-text model supported by optimum-intel can be used. Smaller models (1B-4B parameters) are recommended for edge deployment. For example, OpenGVLab/InternVL3_5-2B.

The script runs:

```bash
optimum-cli export openvino \
    --model <model_id> \
    --task image-text-to-text \
    --trust-remote-code \
    <output_dir>
```

Exported artifacts are stored under `models/<ModelName>/`. 
The export runs once and is cached. To skip export, pass `--model-path` directly.

## Video Preparation

Similarly to model, provide either:

- `--video-path` for a local file
- `--video-url` to download automatically

Downloaded videos are cached under `videos/`. 

## Pipeline Architecture

The pipeline is built dynamically in Python using `Gst.parse_launch`.

```mermaid
graph LR
    A[filesrc] --> B[decodebin3]
    B --> C[gvagenai]
    C --> D[gvametapublish]
    D --> E[gvafpscounter]
    E --> F[gvawatermark]
    F --> G[encode (vah264enc + h264parse + mp4mux)]
    G --> H[filesink]
```

## Setup

```bash
cd samples/gstreamer/python/vlm_alerts
python3 -m venv .vlm-venv
source .vlm-venv/bin/activate
pip install -r requirements.txt
```

> A DL Streamer build that includes the `gvagenai` element is required.

## Running

Required arguments:

- `--prompt`
- `--video-path` or `--video-url`
- `--model-id` or `--model-path`

Example:

```bash
python3 vlm_alerts.py \
    --video-url https://videos.pexels.com/video-files/2103099/2103099-hd_1280_720_60fps.mp4 \
    --model-id OpenGVLab/InternVL3_5-2B \
    --prompt "Is there a police car? Answer yes or no."
```

Optional arguments:

| Argument | Default | Description |
|---|---|---|
| `--device` | `GPU` | Inference device |
| `--max-tokens` | `20` | Maximum tokens in the model response |
| `--frame-rate` | `1.0` | Frames per second passed to `gvagenai` |
| `--videos-dir` | `./videos` | Directory for downloaded videos |
| `--models-dir` | `./models` | Directory for exported models |
| `--results-dir` | `./results` | Directory for output files |

## Output

```
results/<ModelName>-<video_stem>.jsonl
results/<ModelName>-<video_stem>.mp4
```

The `.jsonl` file contains one model response per processed frame and can be used to trigger downstream alerting logic.

### Help

To display all available arguments and defaults:

```bash
python3 vlm_alerts.py --help
