---
name: dlstreamer-coding-agent
description: "Build new DL Streamer video-analytics applications (Python or GStreamer command line). Use when: user describes a vision AI pipeline, wants to create a new sample app, combine elements from existing samples, add detection/classification/VLM/tracking/alerts/recording to a video pipeline, or create custom GStreamer elements in Python. Translates natural-language pipeline descriptions into working DL Streamer code using established design patterns."
argument-hint: "Describe the vision AI pipeline you want to build (e.g. 'detect faces in RTSP stream and save alerts as JSON')"
---

# DL Streamer Coding Agent

Build new DL Streamer video-analytics applications (Python or GStreamer command line) by composing design patterns extracted from existing sample apps.

NOTE: This feature is in PREVIEW stage — expect some rough edges and missing features, and please share your feedback to help us improve it!

## When to Use

- User describes a vision AI processing pipeline in natural language
- User wants to create a new Python sample application built on DL Streamer
- User wants to create a new GStreamer command line using DL Streamer elements
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

> **IMPORTANT:** Never create sample apps in the repository root. Always use the appropriate
> `samples/gstreamer/python/` or `samples/gstreamer/gst_launch/` parent directory. Do this if the skill is invoked from this repo only; if the skill is used by external users, clearly specify in the README.md that they should create a new directory for the sample app following the above layout.

## Procedure

### Execution Overview

After Step 0 (requirements gathering), kick off **all independent long-running tasks in parallel**
via async terminals, then continue with reasoning-heavy work while they complete.

```
Step 0 (gather requirements — interactive)
  │
  ├──► Step 1  (Docker pull — async) ─────────────────────────────────────────┐
  ├──► Step 2a (export scripts → venv + pip install — async) ──► Step 2b ─────┤──► Step 5 (run & validate)
  ├──► Video download (async, if HTTP URL input) ─────────────────────────────┤
  └──► Step 3  (design pipeline — reasoning) ──► Step 4 (generate app) ───────┘
```

**Parallelization rules:**
- Steps 1, 2a, 3, and video download are **fully independent** — start them all immediately after Step 0
- Step 2b (model export) depends on Step 2a (pip install) completing
- Step 4 (generate app) depends on Step 3 (pipeline design) completing
- Step 5 (run & validate) depends on Steps 1, 2b, and 4 all completing

### Reference Lookup

Each reference document is used in **one primary step** to avoid redundant reads:

| Reference | Primary Step | Purpose |
|-----------|-------------|---------|
| [Model Preparation](./references/model-preparation.md) | Step 2 | Prepare AI models in OpenVINO IR format |
| [Pipeline Construction](./references/pipeline-construction.md) | Step 3 | Element selection, pipeline rules, common patterns |
| [Sample Index](./references/sample-index.md) | Step 3 | Existing samples to study before generating code |
| [Design Patterns](./references/design-patterns.md) | Step 3 | Python application structure, patterns, and coding conventions |
| [Debugging Hints](./references/debugging-hints.md) | Step 5 | Docker testing, common gotchas, validation checklist |
| [Requirements Questionnaire](./references/questionnaire.md) | Step 0 | Detailed questions to ask when user prompt is incomplete |

---

### Fast Path (Pattern Table Match)

Before proceeding with the full procedure, check if the user's prompt maps directly to a row in the
[Common Pipeline Patterns table](./references/pipeline-construction.md#common-pipeline-patterns).
If a match is found:

1. Pre-fill the Step 0 checklist fields with the inferred values from the matched row
2. Present the pre-filled values to the user for confirmation (skip the full
   [Requirements Questionnaire](./references/questionnaire.md) unless info is still missing)
3. After the user confirms (or overrides), read **only** the specific design patterns,
   reference sections, and model-preparation sections needed for the final selections
4. Proceed to Steps 1–5 using the confirmed values

### Step 0 — Gather Requirements

Extract the following from the user's prompt:

| Required info | Look for | Default if missing |
|---------------|----------|--------------------|
| **Video input** | File path, HTTP URL, or RTSP URI | — (must ask) |
| **AI model(s)** | Model name/URL and task (detection, classification, VLM, OCR, …) | — (must ask) |
| **Target hardware** | Intel platform, available accelerators (GPU/NPU/CPU) | `Not sure / detect at runtime` |
| **Output format** | Annotated video, JSON, JPEG snapshots, display window | `All of the above` |
| **Application type** | Python app or GStreamer command line | `Python application` |
| **Docker image** | DL Streamer Docker tag | Latest Ubuntu 24 tag (auto-fetched) |

**If the user's prompt explicitly provides all required info** (video input AND model names
are stated by the user, not inferred), proceed directly to Step 1.

**If any required info is missing or was inferred via Fast Path** (not explicitly stated
by the user), you **MUST** present the pre-filled values and ask the user to confirm
or override before proceeding. Use the interactive question tool if available
(e.g. `vscode_askQuestions` in VS Code Copilot), otherwise list the values inline
in chat. Do NOT silently assume defaults and skip confirmation.

### Step 1 — Pull Docker Image (async)

Start the Docker image pull in an **async terminal** immediately after Step 0 completes.

**MANDATORY: Always pull the latest weekly build.** During PREVIEW, the latest weekly
image may contain critical bug fixes not present in older images. Do NOT reuse a
locally cached image without pulling first.

```bash
WEEKLY_TAG=$(curl -s "https://hub.docker.com/v2/repositories/intel/dlstreamer/tags?name=weekly-ubuntu24&page_size=25&ordering=-last_updated" \
    | python3 -c "import sys,json; print(sorted([r['name'] for r in json.load(sys.stdin)['results']])[-1])")
echo "Latest weekly tag: $WEEKLY_TAG"
docker pull "intel/dlstreamer:${WEEKLY_TAG}"
```

If Docker is unavailable, verify a native install: `gst-inspect-1.0 gvadetect 2>&1 | grep Version`.
If neither exists, follow the [Install Guide](../../../docs/user-guide/get_started/install/install_guide_ubuntu.md#option-2-install-docker-image-from-docker-hub-and-run-it).

**Workflow:** develop locally, export models in a Python venv, download video files,
then run the app inside the DLStreamer container with your directory mounted.

### Step 2 — Prepare Models (async)

#### 2a — Create export scripts and kick off venv + pip install

Check which AI models the user wants to use. Search whether the requested or similar models appear in the list of models supported by DL Streamer.

| Model exporter | Typical Models  | Path |
|--------|-------------|------|
| download_public_models.sh | Traditional computer vision models | `samples/download_public_models.sh` |
| download_hf_models.py | HuggingFace models, including VLM models and Transformer-based detection/classification models (RTDETR, CLIP, ViT) | `scripts/download_models/download_hf_models.py` |
| download_ultralytics_models.py | Specialized model downloader for Ultralytics YOLO models | `scripts/download_models/download_ultralytics_models.py` |

If a model is found in one of the above scripts, extract the model download recipe from that script and create a local script in the application directory for exporting the specific model to OV IR format.
If a model does not exist, check the [Model Preparation Reference](./references/model-preparation.md) for instructions on how to prepare and export the model for DL Streamer, then write a new model download/export script using the [Export Models Template](./assets/export-models-template.py).

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

**At the same time**, if the user's input is an HTTP URL, start the video download in
another **async terminal**:

```bash
# Run in async mode — do NOT wait for completion
mkdir -p videos && curl -L -o videos/<video_name>.mp4 "<VIDEO_URL>"
```

Now **proceed immediately** to Step 3 while `pip install`, `docker pull`, and video
download all run in the background.

#### 2b — Run model export (after pip install completes)

Before running the export, confirm the async terminal from Step 2a has completed successfully. If the install failed, diagnose and re-run before continuing.

Once confirmed, run the model export:

```bash
source .<app_name>-export-venv/bin/activate
python3 export_models.py  # or bash export_models.sh
```

### Step 3 — Design Pipeline

Generate a DL Streamer pipeline that captures the user's intent. This step covers both element selection and application structure.

**3a — Select elements and assemble pipeline string**

Use the [Pipeline Construction Reference](./references/pipeline-construction.md) to identify elements for each pipeline stage (source, decode, inference, metadata, sink). Follow the **Pipeline Design Rules** (Rules 1–9) in that reference — prefer auto-negotiation, GPU/NPU inference, `gvaclassify` for OCR, `gvametapublish` for JSON, multi-device assignment on Intel Core Ultra, fragmented MP4 for robustness (Rule 7), audio track handling (Rule 8), avoid unnecessary tee splits (Rule 9).

For common use cases, go straight to file generation using the [use-case → template/pattern mapping table](./references/pipeline-construction.md#common-pipeline-patterns).

For complex cases, consult the [Sample Index](./references/sample-index.md) for relevant reference implementations, then read the specific samples that match the user's use case.

If a user asks for conversion from DeepStream, check the [Converting Guide](../../../docs/user-guide/dev_guide/converting_deepstream_to_dlstreamer.md) for equivalent elements and patterns.

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
- If the application requires Python packages, list them in `requirements.txt`. If the OpenVINO Python runtime is required, pin the same version as the OpenVINO runtime installed with DL Streamer.

### Step 5 — Run, Debug and Validate

**Run in Docker (preferred, especially during PREVIEW):**
```bash
docker run --init --rm \
    -u "$(id -u):$(id -g)" \
    -e PYTHONUNBUFFERED=1 \
    -v "$(pwd)":/app -w /app \
    --device /dev/dri \
    --group-add $(stat -c "%g" /dev/dri/render*) \
    --device /dev/accel \
    --group-add $(stat -c "%g" /dev/accel/accel*) \
    intel/dlstreamer:<WEEKLY_TAG> \
    python3 <app_name>.py
```

> **Autonomous execution — never wait for user confirmation.**
> Launch in async mode, poll `get_terminal_output` every 15–30s until completion.
> Only ask the user when a **decision** is needed (e.g. device change after OOM).
> This applies to all long-running commands: `docker run`, `docker pull`, `pip install`, model export.
>
> **Completion detection:** When the terminal returns an exit code (visible in the
> terminal header or via `get_terminal_output`), the command has **finished** — do NOT
> continue polling or display "awaiting input". Proceed to next step.

**Running locally (native DL Streamer install):**
```bash
python3 -m venv .<app_name>-venv && source .<app_name>-venv/bin/activate
pip install -r requirements.txt
python3 <app_name>.py
```

**Validate:** verify output matches the user's expected results. Use the [Debugging Hints](./references/debugging-hints.md) and [Validation Checklist](./references/debugging-hints.md#validation-checklist) for common gotchas. For continuous/long inputs, send EOS to finalize.

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
See [example prompts](./examples) for inspiration on how to write effective prompts for DL Streamer Coding Agent, and to see how the above procedure can be applied in practice to generate new sample applications.

