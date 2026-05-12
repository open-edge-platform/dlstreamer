# {{APP_TITLE}}

{{APP_DESCRIPTION}}

<!-- Add a short description (2-3 sentences) of what the application does. -->

{{DLSTREAMER_CODING_AGENT_PROMPT}}
<!-- Copy the COMPLETE initial user prompt verbatim, including expected results,
     and validation criteria. Wrap in a blockquote (> ). Do not paraphrase or summarize. -->

{{APP_VISUALIZATION}}

<!-- Include a screenshot from the output video. -->
<!-- ![{{APP_TITLE}}]({{APP_IMAGE}}) -->

<!-- If the input video is from a publicly available or appropriately licensed source
     (for example, https://www.pexels.com/videos/), add a note:
This sample uses a video file from <link> by <author> -->

{{DETAILED_DESCRIPTION}}

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
<!-- Use a Mermaid graph or flowchart to show the pipeline elements and data flow.
     For multi-branch pipelines (tee), use subgraphs (see the vlm_self_checkout example).
     For linear pipelines, use a simple graph LR (see the smart_nvr example). -->

{{PIPELINE_ELEMENTS_LIST}}
<!-- Optional: List each element and its role. Example:
The pipeline uses the following elements:

* __filesrc__ - GStreamer element that reads the video stream from a local file
* __decodebin3__ - GStreamer element that decodes the video stream
* __gvadetect__ - DL Streamer inference element that runs object detection
* __gvawatermark__ - DL Streamer element that overlays detection results on video frames
-->

## Prerequisites

- DL Streamer installed on the host, or a DL Streamer Docker image
- Intel Edge AI system with integrated GPU/NPU (or set device arguments to `CPU`)

### Install Python Dependencies

> **Note:** `export_requirements.txt` includes heavy ML frameworks (PyTorch,
> Ultralytics, PaddlePaddle), needed only for one-time model conversion.
> `requirements.txt` contains only lightweight runtime dependencies.

```bash
python3 -m venv .{{APP_NAME}}-venv
source .{{APP_NAME}}-venv/bin/activate
pip install -r export_requirements.txt -r requirements.txt
```

## Prepare Video and Models (One-Time Setup)

Before running the application, download the input video and export the required models.
This step is performed once; subsequent application runs reuse the prepared assets.

### Download Video

{{VIDEO_DOWNLOAD_INSTRUCTIONS}}
<!-- Add instructions for downloading the test video. Example:

Download the sample video to a local directory:

```bash
mkdir -p videos
curl -L -o videos/sample.mp4 \
    -H "Referer: https://www.pexels.com/" \
    -H "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36" \
    "https://videos.pexels.com/video-files/<ID>/<ID>-hd_<W>_<H>_<FPS>fps.mp4"
```

Alternatively, use any local video file and pass it via `--input`.
-->

### Export Models

The export script downloads the AI models and converts them to OpenVINO IR format.
Converted models are saved under `models/`. This may take several minutes depending on model size and network speed.

```bash
python3 export_models.py
```


## Running the Sample

Once the video and models are prepared (see above), run the application:

```bash
python3 {{APP_NAME}}.py --input videos/sample.mp4
```

You can re-run the application with different runtime options without repeating the preparation step.

{{ADVANCED_USAGE}}
<!-- Optional: Show advanced usage with non-default options. Example:

Run with a different inference device:

```bash
python3 {{APP_NAME}}.py \
    --input /path/to/video.mp4 \
    --detect-model models/yolo11s \
    --detect-device NPU
```
-->

## How It Works

{{HOW_IT_WORKS_SECTIONS}}
<!-- Add a subsection for each major step or custom element. Example:

### STEP 1 — Video Download and Model Export (one-time)

Download the input video and convert models to OpenVINO IR format (see "Prepare Video and Models" above).

### STEP 2 — DL Streamer Pipeline Construction

The application constructs a GStreamer pipeline that combines built-in GStreamer and DL Streamer
elements with custom Python components.

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
<!-- Optional: Include if the sample uses configuration files. Example:

## Configuration Files

| File | Purpose |
|---|---|
| `config/inventory.txt` | List of known inventory items |
| `config/initial_prompt.txt` | VLM model initial prompt |
-->

## Command-Line Arguments

| Argument | Default | Description |
|---|---|---|
{{CLI_ARGUMENTS_TABLE}}
<!-- Example:
| `--input` | `videos/sample.mp4` | Path to input video file or rtsp:// URI |
| `--detect-model` | `models/yolo26s` | Path to OpenVINO IR model converted from Ultralytics |
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

