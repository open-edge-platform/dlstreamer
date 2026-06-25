# {{APP_TITLE}}

{{APP_DESCRIPTION}}

{{DLSTREAMER_CODING_AGENT_PROMPT}}

{{APP_VISUALIZATION}}

{{DETAILED_DESCRIPTION}}

## What It Does

{{NUMBERED_STEPS}}

```mermaid
{{PIPELINE_DIAGRAM}}
```

{{PIPELINE_ELEMENTS_LIST}}

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

## How It Works

{{HOW_IT_WORKS_SECTIONS}}

{{CONFIGURATION_FILES_SECTION}}

## Command-Line Arguments

| Argument | Default | Description |
|---|---|---|
{{CLI_ARGUMENTS_TABLE}}

## Output

Results are written to the `results/` directory:

{{OUTPUT_FILES_LIST}}

