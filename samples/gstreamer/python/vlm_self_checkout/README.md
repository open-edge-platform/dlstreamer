# VLM-assisted Self Checkout

This sample demonstrates a self-checkout pipeline that combines **Computer Vision object detection** with a **Vision-Language Model (VLM)** for intelligent item classification — all inside a single DL Streamer GStreamer pipeline.

![VLM-assisted Self Checkout](vlm_self_checkout.jpg)

The system detects objects on the counter, waits until they are tracked for a configurable duration, selects inventory items matching detected object category, and finally asks the VLM to identify exact inventory items visible. Example VLM prompt (auto-generated from `inventory.txt`):

> *Which of the following items is visible in this image: Coca-Cola Bottle Small, Coca-Cola Bottle Medium, Coca-Cola Bottle Large? Reply only with names of detected items. If no items from the list are visible, reply None.*

Both the object detection model and the VLM run locally on the edge AI device. Note that detailed VLM object classification is accomplished in real time within 1–2 seconds (snapshot taken on an Intel® Core™ Ultra 285H edge device).

## What It Does

1. **Detects** objects in each video frame using a YOLO26s model (`gvadetect`)
2. **Tracks** detected objects across frames (`gvatrack`) and filters by visibility duration
3. **Selects** frames of interest for enhanced VLM classification using a custom Python element (`gvaframeselection_py`)
4. **Classifies** tracked items against a known inventory using a VLM (`gvagenai` with MiniCPM-V-4.5)
5. **Publishes** structured JSONL results and saves snapshot images of classified items
6. **Writes** an annotated output video with watermarked detection and VLM classification results (`gvawatermark`)

```mermaid
flowchart LR
    subgraph Object Detection
        filesrc["📁 filesrc"] --> decodebin3["decodebin3"]
        decodebin3 --> fpsthrottle["gvafpsthrottle\n(30 fps)"]
        fpsthrottle --> gvadetect["gvadetect\n(YOLO26s)"]
        gvadetect --> gvatrack["gvatrack"]
        gvatrack --> tee{tee}
    end

    subgraph Path1["Path 1 — Annotated Video"]
        direction LR
        watermark["gvawatermark"] --> fpscounter["gvafpscounter"]
        fpscounter --> vah264enc["vah264enc"]
        vah264enc --> h264parse["h264parse"]
        h264parse --> mp4mux["mp4mux"]
        mp4mux --> filesink["filesink\n(.mp4)"]
    end

    subgraph Path2["Path 2 — Frame Selection and VLM"]
        direction LR
        lossprev["gvaframeselection_py\n(frame selection)"] --> vapostproc["vapostproc\n(640×360)"]
        vapostproc --> gvagenai["gvagenai\n(MiniCPM-V VLM)"]
        gvagenai --> metapublish["gvametapublish\n(.jsonl)"]
        metapublish --> jpegenc["jpegenc"]
        jpegenc --> multifilesink["📁 multifilesink\n(.jpeg)"]
    end

    tee --> watermark
    tee --> lossprev

    Path2 -. "GObject signal bridge\n(frame-selection / vlm-result)" .-> Path1
```

The pipeline uses a **tee** to split into two parallel paths:
- **Path 1 (Video):** Watermarks detection/VLM results onto frames and encodes to a video file stored on local disk
- **Path 2 (Analytics):** Runs the custom `gvaframeselection_py` frame-selection element, VLM classification, JSONL publishing, and JPEG snapshot export

A **GObject signal bridge** communicates analytics results from Path 2 back to Path 1 for real-time watermark overlay.


## Prerequisites

- DL Streamer installed on host, or DL Streamer docker image
- Intel EdgeAI System with integrated GPU/NPU (or set `--detect-device CPU --genai-device CPU`)
- Python dependencies installed with:

```bash
python3 -m venv .vlm-self-checkout-venv
source .vlm-self-checkout-venv/bin/activate
curl -LO https://raw.githubusercontent.com/openvinotoolkit/openvino.genai/refs/heads/releases/2026/0/samples/export-requirements.txt
pip install -r export-requirements.txt -r requirements.txt
```

## Model Preparation

### Detection (YOLO26s)

The script automatically exports `yolo26s.pt` (included in this directory) to OpenVINO IR format under `models/yolo26s_openvino_model/`.

### VLM (MiniCPM-V-4.5)

The default VLM model is **MiniCPM-V-4.5**, expected at `models/MiniCPM-V-4_5/`. Export it with:

```bash
optimum-cli export openvino \
    --model openbmb/MiniCPM-V-4_5 \
    --task image-text-to-text \
    --trust-remote-code \
    models/MiniCPM-V-4_5/
```

To use a different model, pass `--genai-model-path` pointing to any OpenVINO GenAI-compatible model directory.

## Running the Sample

Basic usage (downloads a sample video and exports the detection model automatically):

```bash
python3 vlm_self_checkout.py
```

With custom model and device settings:

```bash
python3 vlm_self_checkout.py \
    --video-url https://example.com/checkout.mp4 \
    --inventory-file my-custom-inventory.txt \
    --detect-model-id yolo26s \
    --detect-device GPU \
    --genai-model-path models/MiniCPM-V-4_5/ \
    --genai-device GPU
```

## Custom GStreamer Element: `gvaframeselection_py`

The `plugins/python/gvaFrameSelection.py` file implements a custom `GstBase.BaseTransform` element that provides frame-selection logic:

- **Drops** frames with no detected objects
- **Filters out** excluded object types (configured via `excluded_objects.txt`, e.g., `person`, `dining_table`)
- **Tracks** how long each object has been visible using `GstAnalytics.TrackingMtd`
- **Passes** a frame once an object is tracked for at least `threshold` milliseconds (default: 1500)
- **Dynamically updates** the `gvagenai` prompt with matching inventory items from `inventory.txt`

## Configuration Files

| File | Purpose |
|---|---|
| `config/inventory.txt` | List of known inventory items (one per line). Used to generate targeted VLM prompts. |
| `config/excluded_objects.txt` | Object types to ignore during tracking (e.g., `person`, `dining_table`). |

## Command-Line Arguments

| Argument | Default | Description |
|---|---|---|
| `--video-url` | Pexels sample video | URL to download a video from |
| `--detect-model-id` | `yolo26s` | Ultralytics model id for detection |
| `--detect-device` | `GPU` | Device for YOLO detection inference |
| `--threshold` | `0.4` | Detection confidence threshold |
| `--inventory-file` | `config/inventory.txt` | Path to inventory items list |
| `--excluded-objects-file` | `config/excluded_objects.txt` | Path to excluded object types list |
| `--genai-model-path` | `models/MiniCPM-V-4_5` | Path to OpenVINO GenAI model directory |
| `--genai-device` | `GPU` | Device for VLM inference |
| `--genai-prompt` | Self-checkout item description | Initial prompt for VLM inference |

## Output

Results are written to the `results/` directory:

- `vlm_self_checkout-<video>.mp4` — annotated output video with watermarked detections and VLM results
- `vlm_self_checkout-<video>.jsonl` — structured JSON Lines with VLM classification metadata
- `vlm_self_checkout-<video>-*.jpeg` — snapshot images of frames sent to the VLM
