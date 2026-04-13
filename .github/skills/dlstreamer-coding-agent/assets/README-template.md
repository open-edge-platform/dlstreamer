# {{APP_TITLE}}

{{APP_DESCRIPTION}}

<!-- Optional: Include a screenshot from the output video. Omit this line if no image is available. -->
<!-- ![{{APP_TITLE}}]({{APP_IMAGE}}) -->

{{DETAILED_DESCRIPTION}}

{{DLSTREAMER_CODING_AGENT_PROMPT}}
<!-- If the application was generated using DLStreamer Coding Agent, add initial user prompt.
-->

## What It Does

{{NUMBERED_STEPS}}
<!-- Example:
1. **Detects** objects in each video frame using a YOLO model (`gvadetect`)
2. **Tracks** detected objects across frames (`gvatrack`)
3. **Classifies** items using a VLM (`gvagenai`)
4. **Publishes** structured JSONL results (`gvametapublish`)
5. **Writes** an annotated output video with watermarked results (`gvawatermark`)
-->

```mermaid
{{PIPELINE_DIAGRAM}}
```
<!-- Use a Mermaid graph or flowchart showing the pipeline elements and data flow.
     For multi-branch pipelines (tee), use subgraphs (see vlm_self_checkout example).
     For linear pipelines, use a simple graph LR (see smart_nvr example). -->

{{PIPELINE_ELEMENTS_LIST}}
<!-- Optional: List each element and its role. Example:
The pipeline uses the following elements:

* __filesrc__ - GStreamer element that reads the video stream from a local file
* __decodebin3__ - GStreamer element that decodes the video stream
* __gvadetect__ - DLStreamer inference element that detects objects using the detection model
* __gvawatermark__ - DLStreamer element that renders detection results on video frames
-->

## Prerequisites

- DL Streamer installed on host, or DL Streamer docker image
- Intel EdgeAI System with integrated GPU/NPU (or set device arguments to `CPU`)
- Python dependencies installed with:

```bash
python3 -m venv .{{APP_NAME}}-venv
source .{{APP_NAME}}-venv/bin/activate
pip install -r export_requirements.txt -r requirements.txt
```

## Model Preparation

{{MODEL_SECTIONS}}
<!-- Add a subsection for each model used. Example:

### Detection (YOLO26s)

The script automatically downloads `yolo26s.pt` from the Ultralytics hub and converts to OpenVINO IR format under `models/yolo26s_int8_openvino_model/`.
Use `--detect-model-id` to select a different object detection model.

```bash
python3 {{APP_NAME}}.py --detect-model-id <yolo_model_id>
```

### VLM (MiniCPM-V-4.5)

The script automatically downloads `openbmb/MiniCPM-V-4_5` from the HuggingFace hub and converts to OpenVINO IR format under `models/MiniCPM-V-4_5/`.
Use `--vlm-model-id` to select a different VLM model from HuggingFace hub.

```bash
python3 {{APP_NAME}}.py --vlm-model-id <vlm_model_id>
```
-->

## Running the Sample

Basic usage (downloads a sample video and exports models automatically):

```bash
python3 {{APP_NAME}}.py
```

{{ADVANCED_USAGE}}
<!-- Optional: Show advanced usage with non-default options. Example:

With non-default AI models and user-defined input video:

```bash
python3 {{APP_NAME}}.py \
    --video-url https://example.com/video.mp4 \
    --detect-model-id yolo11s \
    --detect-device NPU
```
-->

## How It Works

{{HOW_IT_WORKS_SECTIONS}}
<!-- Add a subsection for each major step or custom element. Example:

### STEP 1 - Model Download and Conversion

The sample downloads an example video file and the detection model from HuggingFace.
The model is converted to OpenVINO IR format using the standard HuggingFace toolchain.

### STEP 2 - DLStreamer Pipeline Construction

The application creates a GStreamer pipeline that combines predefined GStreamer and DLStreamer
elements with custom Python elements.

```python
pipeline = Gst.parse_launch(
    f"filesrc location={video_file} ! decodebin3 ! "
    f"gvadetect model={detection_model} device=GPU ! queue ! "
    f"gvawatermark ! filesink location=output.mp4")
```

### Custom Element: `gvaanalytics_py`

(Describe custom element purpose and logic if applicable)
-->

{{CONFIGURATION_FILES_SECTION}}
<!-- Optional: Include if the sample uses config files. Example:

## Configuration Files

| File | Purpose |
|---|---|
| `config/inventory.txt` | List of known inventory items |
| `config/excluded_objects.txt` | Object types to ignore during tracking |
-->

## Command-Line Arguments

| Argument | Default | Description |
|---|---|---|
{{CLI_ARGUMENTS_TABLE}}
<!-- Example:
| `--video-url` | Pexels sample video | URL to download a video from |
| `--detect-model-id` | `yolo26s` | Ultralytics model id for detection |
| `--detect-device` | `GPU` | Device for detection inference |
-->

## Output

Results are written to the `results/` directory:

{{OUTPUT_FILES_LIST}}
<!-- Example:
- `{{APP_NAME}}-<video>.mp4` — annotated output video with watermarked detections
- `{{APP_NAME}}-<video>.jsonl` — structured JSON Lines with detection/classification metadata
- `output-00.txt` / `output-00.mp4` — chunked video segments with per-segment metadata
-->

## See also
* [Samples overview](../../README.md)
