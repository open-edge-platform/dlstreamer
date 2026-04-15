---
name: dlstreamer-coding-agent
description: "Build new DLStreamer video-analytics applications (Python or GStreamer command line). Use when: user describes a vision AI pipeline, wants to create a new sample app, combine elements from existing samples, add detection/classification/VLM/tracking/alerts/recording to a video pipeline, or create custom GStreamer elements in Python. Translates natural-language pipeline descriptions into working DLStreamer code using established design patterns."
argument-hint: "Describe the vision AI pipeline you want to build (e.g. 'detect faces in RTSP stream and save alerts as JSON')"
---

# DLStreamer Coding Agent

Build new DLStreamer video-analytics applications (Python or GStreamer command line) by composing design patterns extracted from existing sample apps.

NOTE: This feature is in PREVIEW stage — expect some rough edges and missing features, and please share your feedback to help us improve it!

## When to Use

- User describes a vision AI processing pipeline in natural language
- User wants to create a new Python sample application built on DLStreamer
- User wants to create a new GStreamer command line using DLStreamer elements
- User wants to combine elements from multiple existing samples (e.g. detection + VLM + recording)
- User needs to add custom analytics logic or custom GStreamer elements in Python

See [example prompts](./examples) for inspiration.

## Directory Layout for a New Sample App

```
<new_sample_app_name>
├── <app_name>.py or .sh        # Main application (Python or shell script)
├── export_models.py or .sh     # Model download and export script
├── requirements.txt            # Python dependencies for the application
├── export_requirements.txt     # Python dependencies for model export scripts
├── README.md                   # Documentation with instructions on how to install prerequisites and run the sample
├── plugins/                    # Only if custom GStreamer elements are needed
│   └── python/
│       └── <element>.py
├── config/                     # Only if config files are needed
│   └── *.txt / *.json
├── models/                     # Created at runtime (cached model exports)
├── videos/                     # Created at runtime (cached video downloads)
└── results/                    # Created at runtime (output files)
```

## Procedure

### Execution Overview

Steps 1, 2a, and 3 can run **in parallel**. Kick off long-running installs in an async terminal, then continue with reasoning-heavy steps while they complete.

```
Step 1 (runtime env check) ──────────────────┐
Step 2a (export scripts + pip install async) ─┤──► Step 2b (model export) ──► Step 4 (generate app) ──► Step 5 (run & validate)
Step 3 (design pipeline) ────────────────────┘
```

### Reference Lookup

Each reference document is used in **one primary step** to avoid redundant reads:

| Reference | Primary Step | Purpose |
|-----------|-------------|---------|
| [Model Preparation](./references/model-preparation.md) | Step 2 | Prepare AI models in OpenVINO IR format |
| [Pipeline Construction](./references/pipeline-construction.md) | Step 3 | Element selection, pipeline rules, common patterns |
| [Sample Index](./references/sample-index.md) | Step 3 | Existing samples to study before generating code |
| [Design Patterns](./references/design-patterns.md) | Step 3 | Python application structure, patterns, and coding conventions |
| [Debugging Hints](./references/debugging-hints.md) | Step 5 | Docker testing, common gotchas, validation checklist |

---

### Fast Path (Pattern Table Match)

Before proceeding with the full procedure, check if the user's prompt maps directly to a row in the
[Common Pipeline Patterns table](./references/pipeline-construction.md#common-pipeline-patterns).
If a match is found **and** the prompt passes the eligibility checklist below:

1. Skip Step 0 (requirements clarification) — do **NOT** ask clarification questions
2. Read **only** the specific design patterns, reference sections, and model-preparation
   sections needed for this row (see "Fast Path Context Loading" below)
3. Proceed directly to Steps 1–5 using the listed templates and patterns

This fast path avoids unnecessary clarification questions and reduces context loading
for well-defined use cases.

### Step 0 — Clarify Requirements

The user's prompt may be ambiguous or incomplete. Clarify the following before proceeding:

1. **Input source** — video file vs RTSP stream, single vs multi-camera; ask for a specific video file if possible
2. **AI models** — detection, classification, OCR, VLM, etc., and specific models if possible (e.g. "YOLOv8 for detection and PaddleOCRv5 for OCR"). If unspecified, infer the most likely choice from the [Supported Models list](docs/user-guide/supported_models.md).
3. **Pipeline sequence** — e.g. detection → tracking → classification, or detection + VLM in parallel branches
4. **Expected output** — e.g. JSON file with license plate text, annotated video file
5. **Performance requirements** — real-time, batch, etc.

### Step 1 — Check Runtime Environment

First, check if a DLStreamer Docker image is already available locally:
```bash
docker images | grep dlstreamer
```

If a suitable image exists, note its tag for later use. If no image is found, or the image is older than the latest weekly build, pull the latest weekly image:
```bash
# Browse available tags at:
# https://hub.docker.com/r/intel/dlstreamer/tags?name=weekly-ubuntu24
# Then pull a specific tag, e.g.:
docker pull intel/dlstreamer:2026.1.0-20260407-weekly-ubuntu24
```

***Important*** — While the DLStreamer Coding Agent is still in preview, ALWAYS pull the latest weekly build even if the user already has a DLStreamer image, as the latest weekly build may contain important bug fixes and improvements that are not yet in the official release.

If Docker is not available, check if DLStreamer is installed natively on the host:
```bash
gst-inspect-1.0 gvadetect 2>&1 | grep Version
```
If the command returns plugin details, verify that the version matches the latest official release.

If neither DLStreamer Docker image nor native installation is available, download and install DLStreamer docker image following the [Install Docker Image guide](../../../docs/user-guide/get_started/install/install_guide_ubuntu.md#option-2-install-docker-image-from-docker-hub-and-run-it).

Recommended workflow: develop the application locally on your host machine and prepare/export models using a Python virtual environment. Once models are exported to OpenVINO IR format, run the application inside the DLStreamer container with your local directory mounted. This approach maintains development flexibility while leveraging the container for consistent runtime execution.

### Step 2 — Prepare Models (async)

#### 2a — Create export scripts and kick off venv + pip install

Check which AI models the user wants to use. Search whether the requested or similar models appear in the list of models supported by DLStreamer.

| Model exporter | Typical Models  | Path |
|--------|-------------|------|
| download_public_models.sh | Traditional computer vision models | `samples/download_public_models.sh` |
| download_hf_models.py | HuggingFace models, including VLM models and Transformer-based detection/classification models (RTDETR, CLIP, ViT) | `scripts/download_models/download_hf_models.py` |
| download_ultralytics_models.py | Specialized model downloader for Ultralytics YOLO models | `scripts/download_models/download_ultralytics_models.py` |

If a model is found in one of the above scripts, extract the model download recipe from that script and create a local script in the application directory for exporting the specific model to OV IR format.
If a model does not exist, check the [Model Preparation Reference](./references/model-preparation.md) for instructions on how to prepare and export the model for DLStreamer, then write a new model download/export script using the [Export Models Template](./assets/export-models-template.py).

Create the `export_requirements.txt` file if the model export script requires additional Python packages (e.g. HuggingFace transformers, Ultralytics, optimum-cli, etc.). Add comments in `export_requirements.txt` to indicate which model export script requires a specific package. Use **exact pinned versions** from the [Model Preparation Reference → Requirements](./references/model-preparation.md#requirements).

> **CRITICAL — CPU-only PyTorch:** The **first line** of `export_requirements.txt` must be
> `--extra-index-url https://download.pytorch.org/whl/cpu`
> (before any torch-dependent package like `ultralytics` or `nncf`). Without this, pip pulls multi-GB GPU libraries not needed for model export.
> See [Model Preparation Reference → Requirements](./references/model-preparation.md#requirements) for the full template.

**As soon as** `export_requirements.txt` and `export_models.py` are written, start the virtual-environment creation and dependency installation in an **async terminal** so it runs in the background while you continue with Step 3:

```bash
# Run in async mode — do NOT wait for completion
python3 -m venv .<app_name>-export-venv && \
source .<app_name>-export-venv/bin/activate && \
pip install -r export_requirements.txt
```

Now **proceed immediately** to Step 3 while `pip install` runs.

#### 2b — Run model export (after pip install completes)

Before running the export, confirm the async terminal from Step 2a has completed successfully. If the install failed, diagnose and re-run before continuing.

Once confirmed, run the model export:

```bash
source .<app_name>-export-venv/bin/activate
python3 export_models.py  # or bash export_models.sh
```

### Step 3 — Design Pipeline

Generate a DLStreamer pipeline that captures the user's intent. This step covers both element selection and application structure.

**3a — Select elements and assemble pipeline string**

Use the [Pipeline Construction Reference](./references/pipeline-construction.md) to identify elements for each pipeline stage (source, decode, inference, metadata, sink). Follow the **Pipeline Design Rules** (Rules 1–9) in that reference — prefer auto-negotiation, GPU/NPU inference, `gvaclassify` for OCR, `gvametapublish` for JSON, multi-device assignment on Intel Core Ultra, fragmented MP4 for robustness (Rule 7), audio track handling (Rule 8), avoid unnecessary tee splits (Rule 9).

For common use cases, go straight to file generation using the [use-case → template/pattern mapping table](./references/pipeline-construction.md#common-pipeline-patterns).

For complex cases, consult the [Sample Index](./references/sample-index.md) for relevant reference implementations, then read the specific samples that match the user's use case.

**3b — Choose application structure**

For a **CLI application**, the pipeline string from 3a is the deliverable — wrap it in a `gst-launch-1.0` shell script.

For a **Python application**, map the user's description to one or more design patterns using the [Pattern Selection Table](./references/design-patterns.md#pattern-selection-table):
1. Select the **pipeline construction** approach — see [Pattern 1: Pipeline Core](./references/design-patterns.md#pattern-1-pipeline-core)
2. Add **callbacks/probes** as needed
3. Add **custom Python elements** if the user needs inline analytics — check first whether existing GStreamer elements can handle the logic. If not, follow the [Custom Python Element Conventions](./references/design-patterns.md#custom-python-element-conventions).
4. Wire up **argument parsing** and **asset resolution**
5. Add the **pipeline event loop** — see [Pattern 2: Pipeline Event Loop](./references/design-patterns.md#pattern-2-pipeline-event-loop)

### Step 4 — Generate Application

Generate all application files following the directory layout defined at the beginning of this document.

- Read the [Design Patterns Reference](./references/design-patterns.md) for coding conventions and application structure.
- Use the [Application Template](./assets/python-app-template.py) as the starting skeleton for Python apps.
- Use the [README Template](./assets/README-template.md) to generate `README.md` — replace `{{PLACEHOLDERS}}` with application-specific content and remove HTML comments.
- If the application requires Python packages, list them in `requirements.txt`. If the OpenVINO Python runtime is required, pin the same version as the OpenVINO runtime installed with DLStreamer.

### Step 5 — Run, Debug and Validate

**Running in Docker (preferred during PREVIEW):**
Mount the working directory, device drivers, and set correct group permissions:
```bash
docker run --init -it --rm \
    -u "$(id -u):$(id -g)" \
    -v "$(pwd)":/app -w /app \
    --device /dev/dri \
    --group-add $(stat -c "%g" /dev/dri/render*) \
    --device /dev/accel \
    --group-add $(stat -c "%g" /dev/accel/accel*) \
    intel/dlstreamer:<WEEKLY_TAG> \
    python3 <app_name>.py
```
Replace `<WEEKLY_TAG>` with the actual tag discovered in Step 1 (e.g. `2026.1.0-20260407-weekly-ubuntu24`). Pre-create writable output directories (`videos/`, `results/`, `models/`) if needed.

**Running locally (native DLStreamer install):**
```bash
python3 -m venv .<app_name>-venv && source .<app_name>-venv/bin/activate
pip install -r requirements.txt
python3 <app_name>.py  # or bash <app_name>.sh
```

**Validate:**
Once the environment is set up, update the instructions in the generated README.md file and verify that the application runs correctly when following them. If the user provided a natural-language description of the expected output, verify that the output matches the description (e.g. check that JSONL files have the expected fields, check that video outputs have the expected overlays, etc.).

If the application is running for a long time (>1 minute), make sure there is some output in the terminal to indicate progress and avoid leaving the user wondering if the application is stuck. Switch focus to the terminal output so the user can see logs and progress.
If the application has a continuous input stream (RTSP camera source) or large input video files, send an EOS signal to the application.

Refer to the [Debugging Hints](./references/debugging-hints.md) for Docker testing conventions, common gotchas, and the post-run [Validation Checklist](./references/debugging-hints.md#validation-checklist).

---

### Post-Task — Report Coding Agent Analytics

> **MANDATORY while PREVIEW:** This step MUST always be executed — do NOT skip it.
> Report the analytics table immediately after the application is validated, in the
> same response as the validation results.

After the application is working, report timing metrics for the activity:

1. **AI reasoning time** — understanding the prompt, designing the pipeline, writing code
2. **Environment setup time** — waiting for `pip install`, model export, Docker image pull
3. **Debug and validation time** — running the application, checking outputs, fixing issues
4. **User wait time** — waiting for user input or confirmation
5. **Total activity time** (phases may overlap, so total ≠ sum of individual phases)

## Examples
See [example prompts](./examples) for inspiration on how to write effective prompts for DLStreamer Coding Agent, and to see how the above procedure can be applied in practice to generate new sample applications.

