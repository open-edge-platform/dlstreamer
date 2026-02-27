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

Exported OpenVINO artifacts are stored locally in the project directory after the first run.

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
   curl -LO https://raw.githubusercontent.com/openvinotoolkit/openvino.genai/refs/heads/releases/2026/0/samples/export-requirements.txt
   pip install -r export-requirements.txt -r requirements.txt
   ```

If you need to update dependencies, regenerate the pinned versions in [requirements.txt](requirements.txt) from a known-good environment.

## Running

Provide a local video file:

```code
python3 face_detection_and_classification.py /path/to/video.mp4
```

Or run without arguments to download and use a default video:

```code
python3 face_detection_and_classification.py
```

The output video will be saved alongside the input file with the suffix `_output.mp4`.

## Sample Output

The script prints the pipeline string and produces an output video annotated with detections and classification results.
