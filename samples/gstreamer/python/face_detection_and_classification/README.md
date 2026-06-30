# Face Detection and Classification

This sample demonstrates how to download face detection and classification models from Hugging Face, export them to OpenVINO™ IR, and run inference in a GStreamer pipeline.

## How It Works

The script demonstrates the full flow for preparing models and running a DL Streamer pipeline in Python. Each stage is marked in code with STEP comments:

**STEP 1 — Prepare face detection model**
Download the YOLOv8 face detector from Hugging Face and export it to OpenVINO IR using the Ultralytics exporter.

**STEP 2 — Prepare the classification model**
Use optimum-cli to download the face age classifier from Hugging Face and export it to OpenVINO IR.

**STEP 3 — Build and run the pipeline**
Use GStreamer and DL Streamer elements to build a pipeline, run inference with `gvadetect` and `gvaclassify`, annotate frames with `gvawatermak`, and encode the output to MP4.

```mermaid
graph LR
    A[filesrc] --> B[decodebin3]
    B --> C[gvadetect]
    C --> D[gvaclassify]
    D --> E[gvafpscounter]
    E --> F[gvawatermark]
    F --> G["encode (vah264enc + h264parse + mp4mux)"]
    G --> H[filesink]
```

If no input video is provided, a default video is downloaded and used automatically.

## Models

This demo uses the following models from Hugging Face:

* Face detection: `arnabdhar/YOLOv8-Face-Detection`
* Classification: `dima806/fairface_age_image_detection`

Exported OpenVINO artifacts are stored in the current working directory after the first run.

## Reproducible setup

This project pins all dependencies in [requirements.txt](requirements.txt) for deterministic installs.

### Install

1. Create and activate a virtual environment:
```code
   python3 -m venv .face_det_cls_venv
   source .face_det_cls_venv/bin/activate
   ```

2. Install dependencies:
```code
   curl -LO https://raw.githubusercontent.com/openvinotoolkit/openvino.genai/refs/heads/releases/2026/2/samples/export-requirements.txt
   pip install -r export-requirements.txt -r requirements.txt
   ```

If you need to update dependencies, regenerate the pinned versions in [requirements.txt](requirements.txt) from a known-good environment.

## Running

The sample accepts up to three positional arguments:

```code
python3 face_detection_and_classification.py [INPUT] [DEVICE] [OUTPUT]
```

* `INPUT`  - local video file. Omit (or pass an empty string) to download and use the default video.
* `DEVICE` - inference device, `CPU` or `GPU` (default: `GPU`).
* `OUTPUT` - output mode (default: `file`):
  * `file` - annotate frames and encode an MP4 saved alongside the input with the suffix `_output.mp4`.
  * `json` - write deterministic inference results as json-lines to `output.json` in the working directory.

Examples:

```code
# Default video, GPU, encode annotated MP4
python3 face_detection_and_classification.py

# Local file on CPU, write json-lines to output.json
python3 face_detection_and_classification.py /path/to/video.mp4 CPU json
```

A thin wrapper script is also provided for convenience:

```code
./face_detection_and_classification.sh /path/to/video.mp4 GPU json
```

The detection and classification model revisions and export precision are pinned so the `json`
output is reproducible across runs.

## Sample Output

In `file` mode the script produces an output video annotated with detections and classification
results. In `json` mode it writes one json-lines record per frame to `output.json`.
