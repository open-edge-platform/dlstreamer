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

**Output location** — create the sample directory under the correct parent based on application type:

| Application Type | Parent Directory |
|-----------------|-----------------|
| Python application | `samples/gstreamer/python/<new_sample_app_name>/` |
| GStreamer command line | `samples/gstreamer/gst_launch/<new_sample_app_name>/` |

```
samples/gstreamer/python/<new_sample_app_name>/   # or gst_launch/ for CLI apps
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
> `samples/gstreamer/python/` or `samples/gstreamer/gst_launch/` parent directory.

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
  └──► Step 3  (design pipeline — reasoning) ──► Step 4 (generate app) ──────┘
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

---

### Fast Path (Pattern Table Match)

Before proceeding with the full procedure, check if the user's prompt maps directly to a row in the
[Common Pipeline Patterns table](./references/pipeline-construction.md#common-pipeline-patterns).
If a match is found, **pre-populate** the Step 0 questionnaire with the inferred values
from the matched row:

1. Fill in each questionnaire field with the best-matching value from the pattern row
   (e.g. model names, tasks, output formats, recommended Docker image)
2. Mark pre-populated options as `recommended` so the user sees them as defaults
3. **Still present the questionnaire** via `vscode_askQuestions` for explicit user confirmation
4. After the user confirms (or overrides), read **only** the specific design patterns,
   reference sections, and model-preparation sections needed for the final selections
5. Proceed to Steps 1–5 using the confirmed values

This fast path reduces the effort needed to fill in the questionnaire while still giving
the user a chance to review and override any auto-inferred choices.

### Step 0 — Gather Requirements Interactively

Use the `vscode_askQuestions` tool to collect pipeline requirements from the user in a
**single call**. Pre-fill options based on the user's initial prompt where possible,
but present all questions for explicit confirmation.

> If the user's prompt matches a Fast Path row, pre-fill these questions with the inferred
> values and mark them as `recommended`. The user can confirm or override before proceeding.

#### Section 1 — Input

| Header | Question | Options |
|--------|----------|---------|
| `Input Type` | What type of video input? | `Local file path`, `HTTP URL`, `RTSP stream URI` |
| `Input Value` | Provide the path or URL (e.g. `/path/to/video.mp4`, `https://...`, `rtsp://...`) | Free text |

#### Section 2 — AI Models

| Header | Question | Options |
|--------|----------|---------|
| `Model 1` | First model URL/name (e.g. `yolo11n`, `PaddlePaddle/PP-OCRv5_server_rec`) | Free text |
| `Model 1 Task` | What does this model do? | `Object detection`, `Classification`, `OCR / text recognition`, `VLM / generative AI`, `Segmentation`, `Pose estimation`, `Other` |
| `Model 2 (optional)` | Second model URL/name. Leave empty if not needed. | Free text |
| `Model 2 Task` | What does the second model do? (skip if no Model 2) | Same options as Model 1 Task |

> Add more `Model N` + `Model N Task` pairs only if the use case clearly requires 3+ models.
>
> The task selection maps directly to DL Streamer elements:
> | Task | Element |
> |------|---------|
> | Object detection | `gvadetect` |
> | Classification / OCR / Pose estimation / Segmentation | `gvaclassify` |
> | VLM / generative AI | `gvagenai` |
>
> The agent auto-infers the source ecosystem (HuggingFace / Ultralytics / direct URL)
> from the model URL or name — no need to ask the user separately.

#### Section 3 — Target Environment

| Header | Question | Options |
|--------|----------|---------|
| `Intel Platform` | What Intel hardware will this run on? | `Intel Core Ultra 3 (Panther Lake) — CPU + Xe3 GPU + NPU`, `Intel Core Ultra 2 (Lunar Lake / Arrow Lake) — CPU + GPU + NPU`, `Intel Core Ultra 1 (Meteor Lake) — CPU + GPU + NPU`, `Intel Core (older, no NPU) — CPU + GPU`, `Intel Xeon (server) — CPU only`, `Intel Arc discrete GPU`, `Not sure / detect at runtime` |
| `Available Accelerators` | Which accelerators are available? (select all that apply) | `GPU (/dev/dri/renderD128)`, `NPU (/dev/accel/accel0)`, `CPU only` (multiSelect) |

> The agent uses these answers to apply **Rule 6 — Device Assignment Strategy** from
> [Pipeline Construction Reference](./references/pipeline-construction.md#rule-6--device-assignment-strategy-for-intel-core-ultra)
> when setting `device=` and `batch-size=` on inference elements.
> For advanced tuning (multi-GPU selection, pre-process backends, MULTI: device),
> refer to the docs:
> - [Performance Guide](../../../docs/user-guide/dev_guide/performance_guide.md) — batch-size, multi-stream, memory types
> - [GPU Device Selection](../../../docs/user-guide/dev_guide/gpu_device_selection.md) — multi-GPU systems
> - [Optimizer](../../../docs/user-guide/dev_guide/optimizer.md) — auto-tuning tool

#### Section 4 — Output

| Header | Question | Options |
|--------|----------|---------|
| `Output Format` | What outputs do you need? | `Annotated video (.mp4)`, `JSON metadata (.jsonl)`, `JPEG snapshots`, `Display window`, `All of the above` (multiSelect) |

#### Section 5 — Application Type

| Header | Question | Options |
|--------|----------|---------|
| `Application Type` | Python application or Gstreamer command line? | `Python application` (recommended), `Gstreamer command line` |

#### Section 6 — Docker Image

Before presenting this question, fetch the latest available tags by running in a terminal:

```bash
curl -s "https://hub.docker.com/v2/repositories/intel/dlstreamer/tags/?page_size=30&ordering=last_updated" \
  | python3 -c "
import sys, json
data = json.load(sys.stdin)
release, weekly = [], []
for t in data.get('results', []):
    name = t['name']
    if name in ('latest',) or 'sources' in name or 'dev' in name or 'rc' in name:
        continue
    if 'weekly' in name:
        weekly.append(name)
    else:
        release.append(name)
print('RELEASE:', ' '.join(release[:4]))
print('WEEKLY:', ' '.join(weekly[:4]))
"
```

Use the output to populate the options dynamically:

| Header | Question | Options |
|--------|----------|---------|
| `Docker Image` | Which DL Streamer Docker image? | Dynamically populated (see below), allowFreeformInput=true |

**Option construction from fetched tags:**
1. First option: the latest weekly Ubuntu 24 tag from the `WEEKLY` line — mark as `recommended`
2. Next 1–2 options: remaining weekly tags (Ubuntu 22, previous week)
3. Next 2–3 options: release tags from the `RELEASE` line
4. Prefix all labels with `intel/dlstreamer:`

> The user can also type a custom tag (e.g. a locally built image).
> Full tag list: https://hub.docker.com/r/intel/dlstreamer/tags

Store the selected image as `<DOCKER_IMAGE>` for use in all subsequent `docker pull`
and `docker run` commands.

### Step 1 — Pull Docker Image (async)

Start the Docker image pull in an **async terminal** immediately after Step 0 completes.
Use the `<DOCKER_IMAGE>` selected by the user in Step 0 Section 6:

```bash
# Run in async mode — do NOT wait for completion
docker pull <DOCKER_IMAGE>
```

If no Docker image was selected (user has a native DL Streamer install), skip this step and verify:
```bash
gst-inspect-1.0 gvadetect 2>&1 | grep Version
```

> **Proceed immediately** to Step 2 while the pull runs in the background.

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
    <DOCKER_IMAGE> \
    python3 <app_name>.py
```
Replace `<DOCKER_IMAGE>` with the image selected in Step 0 Section 6. Omit
`--device /dev/accel` and its `--group-add` when NPU is not available on the host.
Pre-create writable output directories (`videos/`, `results/`, `models/`) if needed.

**Running locally (native DL Streamer install):**
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
See [example prompts](./examples) for inspiration on how to write effective prompts for DL Streamer Coding Agent, and to see how the above procedure can be applied in practice to generate new sample applications.

