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

- DL Streamer installed on host, or DL Streamer docker image
- Intel EdgeAI system with integrated GPU/NPU (or set device arguments to `CPU`)
- Python dependencies installed with:

> **Note:** Model export and pipeline runtime use separate virtual environments to avoid dependency conflicts.

```bash
python3 -m venv .{{APP_NAME}}-venv
source .{{APP_NAME}}-venv/bin/activate
pip install -r export_requirements.txt -r requirements.txt
```

## Model Preparation

{{MODEL_SECTIONS}}

## Running the Sample

Basic usage (downloads a sample video and exports models automatically):

```bash
python3 {{APP_NAME}}.py
```

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

## See also

* [Samples overview](../../README.md)
