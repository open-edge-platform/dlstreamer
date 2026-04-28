---
name: dlstreamer-coding-agent
description: "Build new DL Streamer video-analytics applications (Python or GStreamer command line). Use when: user describes a vision AI pipeline, wants to create a new sample app, combine elements from existing samples, add detection/classification/VLM/tracking/alerts/recording to a video pipeline, or create custom GStreamer elements in Python. Translates natural-language pipeline descriptions into working DL Streamer code using established design patterns."
argument-hint: "Describe the vision AI pipeline you want to build (e.g. 'detect faces in RTSP stream and save alerts as JSON')"
---

# DL Streamer Coding Agent

Build new DL Streamer video-analytics applications (Python or GStreamer command line) by composing design patterns extracted from existing sample apps.

NOTE: This feature is in PREVIEW stage — expect some rough edges and missing features, and please share your feedback to help us improve it!

## When to Use

- User describes a vision AI pipeline in natural language
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
├── README.md                   # Setup and usage instructions
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

After Step 0 (requirements gathering), kick off **all independent long-running tasks in parallel**
via async terminals, then continue with reasoning-heavy work while they complete.

```
Step 0 (gather requirements — interactive)
  │
  ├──► Step 1  (Docker pull — async) ───────────────────────────────────────┐
  ├──► Step 2a (export scripts + pip install — async) ──► Step 2c (export)──┤
  ├──► Step 2b (video download — async) ────────────────────────────────────┤───► Step 5 (run & validate)
  └──► Step 3  (design pipeline — reasoning) ──► Step 4 (generate app) ─────┘
```

**Parallelization rules:**
- Steps 1, 2a, 2b, and 3 are **fully independent** — start them all immediately after Step 0
- Step 2c (model export) depends on Step 2a (pip install) completing
- Step 4 (generate app) depends on Step 3 (pipeline design) completing
- Step 5 (run & validate) depends on Steps 1, 2c, and 4 all completing

### Reference Lookup

Each reference document is used in **one primary step** to avoid redundant reads:

| Reference | Primary Step | Purpose |
|-----------|-------------|---------|
| [Requirements Questionnaire](./references/questionnaire.md) | Step 0 | Detailed questions to ask when user prompt is incomplete |
| [Model Preparation](./references/model-preparation.md) | Step 2 | Prepare AI models in OpenVINO IR format |
| [Pipeline Construction](./references/pipeline-construction.md) | Step 3 | Element selection, pipeline rules, common patterns |
| [Sample Index](./references/sample-index.md) | Step 3 | Existing samples to study before generating code |
| [Design Patterns](./references/design-patterns.md) | Step 3 | Python application structure, patterns, and coding conventions |
| [Debugging Hints](./references/debugging-hints.md) | Step 5 | Docker testing, common gotchas, validation checklist |

---

### Fast Path (Pattern Table Match)

Before proceeding with the full procedure, check if the user's prompt maps directly to a row in the
[Common Pipeline Patterns table](./references/pipeline-construction.md#common-pipeline-patterns).
If a match is found:

1. Pre-fill Step 0 fields from the matched row
2. Present the pre-filled values to the user for confirmation (skip the full
   [Requirements Questionnaire](./references/questionnaire.md) unless info is still missing)
3. After the user confirms (or overrides), read **only** the design patterns,
   reference sections, and model-preparation sections needed for the confirmed selections
4. Proceed to Steps 1–5

### Step 0 — Gather Requirements

Extract the following from the user's prompt:

| Required info | Look for | Default if missing |
|---------------|----------|--------------------|
| **Video input** | File path, HTTP URL (for download), or RTSP URI | — (must ask) |
| **AI model(s)** | Model name/URL and task (detection, classification, VLM, OCR, …) | — (must ask) |
| **Target hardware** | Intel platform, available accelerators (GPU/NPU/CPU) | `Not sure / detect at runtime` |
| **Output format** | Annotated video, JSON, JPEG snapshots, display window | `All of the above` |
| **Application type** | Python app or GStreamer command line | `Python application` |
| **Docker image** | DL Streamer Docker tag | Latest Ubuntu 24 tag (auto-fetched) |

**If the user's prompt explicitly provides all required info** (video input AND model names
are explicitly stated, not inferred), proceed directly to Step 1.

**If any required info is missing or was inferred via Fast Path** (not explicitly stated
by the user), you **MUST** present the pre-filled values and ask the user to confirm
or override before proceeding. Use the interactive question tool if available
(e.g. `vscode_askQuestions` in VS Code Copilot), otherwise list the values inline
in chat. Do NOT silently assume defaults and skip confirmation.

### Step 1 — Pull Docker Image (async)

Start the Docker image pull in an **async terminal** immediately after Step 0 completes.

**Always pull the latest weekly build.** During PREVIEW, the latest weekly
image may contain critical bug fixes not present in older images. Do NOT reuse a
locally cached image without pulling first.

```bash
WEEKLY_TAG=$(curl -s "https://hub.docker.com/v2/repositories/intel/dlstreamer/tags?name=weekly-ubuntu24&page_size=25&ordering=-last_updated" \
    | python3 -c "import sys,json; print(sorted([r['name'] for r in json.load(sys.stdin)['results']])[-1])")
echo "Latest weekly tag: $WEEKLY_TAG"
docker pull "intel/dlstreamer:${WEEKLY_TAG}"
```

### Step 2 — Prepare Models and Video (async)

#### 2a — Create export scripts and kick off venv + pip install

Check whether the requested models (or similar ones) appear in the model exporters bundled with DL Streamer.

| Model exporter | Typical Models  | Path |
|--------|-------------|------|
| download_public_models.sh | Traditional computer vision models | `samples/download_public_models.sh` |
| download_hf_models.py | HuggingFace models, including VLM models and Transformer-based detection/classification models (RTDETR, CLIP, ViT) | `scripts/download_models/download_hf_models.py` |
| download_ultralytics_models.py | Specialized model downloader for Ultralytics YOLO models | `scripts/download_models/download_ultralytics_models.py` |

If a model is found, extract its download recipe and create a local `export_models.py` in the application directory.
If a model is not listed, check the [Model Preparation Reference](./references/model-preparation.md) for export instructions, then write a new script using the [Export Models Template](./assets/export-models-template.py).

Create the `export_requirements.txt` file if the model export script requires additional Python packages (e.g. HuggingFace transformers, Ultralytics, optimum-cli, etc.). Add comments in `export_requirements.txt` to indicate which model export script requires a specific package. Use **exact pinned versions** from the [Model Preparation Reference → Requirements](./references/model-preparation.md#requirements).

> **CRITICAL — CPU-only PyTorch:** The **first line** of `export_requirements.txt` must be
> `--extra-index-url https://download.pytorch.org/whl/cpu`
> (before any torch-dependent package like `ultralytics` or `nncf`). Without this, pip pulls multi-GB GPU libraries not needed for model export.
> See [Model Preparation Reference → Requirements](./references/model-preparation.md#requirements) for the full template.

Once both files are written, start venv creation and pip install in an **async terminal**:

```bash
# Run in async mode — do NOT wait for completion
python3 -m venv .<app_name>-export-venv && \
source .<app_name>-export-venv/bin/activate && \
pip install -r export_requirements.txt
```

#### 2b — Download video to local directory

If the user provided an HTTP URL for video input, download it now:

```bash
mkdir -p videos && curl -L -o videos/<video_name>.mp4 \
    -H "Referer: https://www.pexels.com/" \
    -H "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36" \
    "<DIRECT_VIDEO_URL>"
```

The application itself should **not** download videos — it accepts only `--input`
pointing to a local file or RTSP URI. Document download steps in the README.

> **Pexels page URLs → direct file URLs:** A Pexels *page* URL
> (`https://www.pexels.com/video/<slug>-<ID>/`) is **not** a direct download link.
> Scrape the page with `curl -s` and search the HTML for
> `videos.pexels.com/video-files/` links to get the actual `.mp4` URL.
> Do not guess resolution or FPS — they vary per video.
> If scraping fails, ask the user for the direct URL.

> **Git LFS warning:** Videos from `edge-ai-resources` may return HTML instead of
> video data. Verify: `file videos/sample.mp4 | grep -q "ISO Media"`.
> Prefer Pexels direct URLs as default test videos.

Proceed to Step 3 while `pip install` and `docker pull` run in the background.

#### 2c — Run model export (after pip install completes)

Before running the export, confirm the async terminal from Step 2a has completed successfully.
If the install failed, diagnose and re-run before continuing.

Once confirmed, run the model export:

```bash
source .<app_name>-export-venv/bin/activate
python3 export_models.py  # or bash export_models.sh
```

### Step 3 — Design Pipeline

Design a DL Streamer pipeline that fulfils the user's requirements. This step covers element selection and application structure.

**3a — Select elements and assemble pipeline string**

Use the [Pipeline Construction Reference](./references/pipeline-construction.md) to identify elements for each pipeline stage (source, decode, inference, metadata, sink). Follow the **Pipeline Design Rules** (Rules 1–10) in that reference.

For common use cases, go straight to file generation using the [use-case → template/pattern mapping table](./references/pipeline-construction.md#common-pipeline-patterns).

For complex cases, consult the [Sample Index](./references/sample-index.md) for relevant reference implementations, then read the specific samples that match the user's use case.

If a user asks for conversion from DeepStream, check the [Converting Guide](../../../docs/user-guide/dev_guide/converting_deepstream_to_dlstreamer.md) for equivalent elements and patterns.

**3b — Choose application structure**

For a **CLI application**, the pipeline string from 3a is the deliverable — wrap it in a `gst-launch-1.0` shell script.

For a **Python application**, map the user's description to one or more design patterns using the [Pattern Selection Table](./references/design-patterns.md#pattern-selection-table):
1. Select the **pipeline construction** approach — see [Pattern 1: Pipeline Core](./references/design-patterns.md#pattern-1-pipeline-core)
2. Add **callbacks/probes** as needed
3. Add **custom Python elements** if the user needs inline analytics — check first whether existing GStreamer elements can handle the logic. If not, follow the [Custom Python Element Conventions](./references/design-patterns.md#custom-python-element-conventions).
4. Wire up **argument parsing**
5. Add the **pipeline event loop** — see [Pattern 2: Pipeline Event Loop](./references/design-patterns.md#pattern-2-pipeline-event-loop)

### Step 4 — Generate Application

Generate all application files following the directory layout defined at the beginning of this document.

- Read the [Design Patterns Reference](./references/design-patterns.md) for coding conventions and application structure.
- Use the [Application Template](./assets/python-app-template.py) as the starting skeleton for Python apps.
- Use the [README Template](./assets/README-template.md) to generate `README.md` — replace `{{PLACEHOLDERS}}` with application-specific content and remove HTML comments.
- If the application requires Python packages, list them in `requirements.txt`. If the OpenVINO Python runtime is required, pin the same version as the OpenVINO runtime installed with DL Streamer.

### Step 5 — Run, Debug and Validate

**Run in Docker (required in PREVIEW stage):**
```bash
docker run --init --rm \
    -u "$(id -u):$(id -g)" \
    -e PYTHONUNBUFFERED=1 \
    -v "$(pwd)":/app -w /app \
    -v /path/to/models:/app/models:ro \
    -v /path/to/videos:/app/videos:ro \
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

**Validate:** check that output matches the user's expected results. Use the [Debugging Hints](./references/debugging-hints.md) and [Validation Checklist](./references/debugging-hints.md#validation-checklist) for common gotchas. For continuous or long inputs, send EOS to finalize.

---

### Post-Task — Report Coding Agent Analytics

> **MANDATORY while PREVIEW — do NOT skip this step.**
> Report the analytics table immediately after the application is validated, in the
> same response as the validation results.

After the application is working, report timing metrics:

1. **AI reasoning time** — understanding the prompt, designing the pipeline, writing code
2. **Environment setup time** — waiting for `pip install`, model export, Docker image pull
3. **Debug and validation time** — running the application, checking outputs, fixing issues
4. **User wait time** — waiting for user input or confirmation
5. **Total activity time** (phases may overlap, so total ≠ sum of individual phases)

## Examples
See [example prompts](./examples) for inspiration and practical demonstrations of the procedure.

