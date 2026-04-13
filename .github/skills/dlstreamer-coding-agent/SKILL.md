---
name: dlstreamer-coding-agent
description: "Build new DLStreamer Python video-analytics applications. Use when: user describes a vision AI pipeline, wants to create a new sample app, combine elements from existing samples, add detection/classification/VLM/tracking/alerts/recording to a video pipeline, or create custom GStreamer elements in Python. Translates natural-language pipeline descriptions into working DLStreamer Python code using established design patterns."
argument-hint: "Describe the vision AI pipeline you want to build (e.g. 'detect faces in RTSP stream and save alerts as JSON')"
---

# DLStreamer Coding Agent

Build new DLStreamer Python video-analytics applications by composing design patterns extracted from existing sample apps.

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

### Step 0 — Fast Path (Pattern Table Match)

Before proceeding with the full procedure, check if the user's prompt maps directly to
a row in the [Common Pipeline Patterns table](./references/pipeline-construction.md#common-pipeline-patterns).
If a match is found **and** the prompt is unambiguous (input source, model type, and
expected output are all clear or can be confidently inferred):

1. Skip Step 1 (prompt refinement)
2. Read **only** the specific Design Patterns listed in the matching row (not all references)
3. Proceed directly to Steps 2–6 using the listed templates and patterns

This fast path avoids unnecessary clarification questions and reduces context loading
for well-defined use cases.

### Step 1 — Refine User Prompt

The user's prompt may be ambiguous or incomplete. Before proceeding further, make sure the following details are clarified:
1) Input source (video file vs RTSP stream, single vs multi-camera, etc.); ask for a specific video file if possible
2) AI model types (detection, classification, OCR, VLM, etc.) and specific models if possible (e.g. "YOLOv8 for detection and PaddleOCRv5 for OCR")
If the user does not have specific models in mind, try to infer the most likely model choice based on the task description and the list of models supported by DLStreamer (`../../../../docs/user-guide/supported_models.md`).
3) Sequence of operations in the pipeline (e.g. detection → tracking → classification, or detection + VLM in parallel branches, etc.)
4) Expected output (e.g. JSON file with license plate text, annotated video file, etc.)
5) Performance requirements (e.g. real-time processing, batch processing, etc.)

### Step 2 — Identify Models and Start Environment Setup (early, async)

> **Parallelization rule:** Steps 2, 3, and 4 overlap. The venv creation and `pip install`
> from Step 2 are **network-bound** and take minutes but require **no reasoning**. Start
> them in an **async terminal** immediately after creating the requirements file, then
> continue with Steps 3 and 4 (Docker check + pipeline design) while the install runs
> in the background. Come back to run the actual model export in Step 2b only after
> `pip install` has finished.

**2a — Create export scripts and kick off venv + pip install**

Check which AI models the user wants to use. Search whether the requested or similar models appear in the list of models supported by DLStreamer.

| Model exporter | Typical Models  | Path |
|--------|-------------|------|
| download_public_models.sh | Traditional computer vision models | `samples/download_public_models.sh` |
| download_hf_models.py | HuggingFace models, including VLM models and Transformer-based detection/classification models (RTDETR, CLIP, ViT) | `scripts/download_models/download_hf_models.py` |
| download_ultralytics_models.py | Specialized model downloader for Ultralytics YOLO models | `scripts/download_models/download_ultralytics_models.py` |

If a model is found in one of the above scripts, extract the model download recipe from that script and create a local script in the application directory for exporting the specific model to OV IR format; add model export instructions to the application README.
If a model does not exist, check the [Model Preparation Reference](./references/model-preparation.md) for instructions on how to prepare and export the model for DLStreamer, then write a new model download/export script using the [Export Models Template](./assets/export-models-template.py) as a starting point and add instructions to the application README.

Create the `export_requirements.txt` file if the model export script requires additional Python packages (e.g. HuggingFace transformers, Ultralytics, optimum-cli, etc.). Add comments in `export_requirements.txt` to indicate which model export script requires a specific package. Use **exact pinned versions** from the [Model Preparation Reference → Requirements](./references/model-preparation.md#requirements).

**As soon as** `export_requirements.txt` and `export_models.py` are written, start the
virtual-environment creation and dependency installation in an **async terminal** so it
runs in the background while you continue reasoning:

```bash
# Run in async mode — do NOT wait for completion
python3 -m venv .<app_name>-export-venv && \
source .<app_name>-export-venv/bin/activate && \
pip install -r export_requirements.txt
```

> **Important:** When running terminal commands that may take a long time (e.g. `pip install`,
> model downloads, model export), do **not** pipe output through `tail`, `head`, or other
> filters that hide progress. Let the full output stream to the terminal so the user can
> see download/install progress and is not left waiting with no feedback.

Now **proceed immediately** to Steps 3 and 4 while `pip install` runs.

**2b — Run model export (after pip install completes)**

After Steps 3 and 4 are done (or earlier, if `pip install` finished), check the async
terminal output to confirm all dependencies were installed successfully, then run the
model export:

```bash
source .<app_name>-export-venv/bin/activate
python3 export_models.py  # or bash export_models.sh
```

### Step 3 — Check and Setup Deployment Environment

Check if the user's machine has DLStreamer installed:
```bash
gst-inspect-1.0 gvadetect 2>&1 | grep Version
```

The command should return plugin details. If it does, check if the plugin version matches the latest official release of DLStreamer.

If the plugin is not found, or the version is older than the latest release, download the latest weekly DLStreamer docker image.

**Discovering the latest Docker tag:**
```bash
# Check already-pulled images:
docker images | grep dlstreamer

# If no local image exists, browse available tags at:
# https://hub.docker.com/r/intel/dlstreamer/tags?name=weekly-ubuntu24
# Then pull a specific tag, e.g.:
docker pull intel/dlstreamer:2026.1.0-20260407-weekly-ubuntu24
```

***Important*** — While the DLStreamer Coding Agent is still in preview, ALWAYS download the latest weekly build even if the user has the latest official version of DLStreamer installed, as the latest weekly build may contain important bug fixes and improvements that are not yet in the official release.

Recommended workflow: develop the application locally on your host machine and prepare/export models using a Python virtual environment. Once models are exported to OpenVINO IR format, run the application inside the DLStreamer container with your local directory mounted. This approach maintains development flexibility while leveraging the container for consistent runtime execution.

### Step 4 — Define DLStreamer Pipeline from User Description

Generate a DLStreamer pipeline string that captures the user's intent using DLStreamer elements. Use the [Pipeline Construction Reference](./references/pipeline-construction.md) to identify which elements to use for each part of the pipeline (e.g. source, decode, inference, metadata handling, sink).

For common use cases, go straight to file generation using the [use-case → template/pattern mapping table](./references/pipeline-construction.md#common-pipeline-patterns) in the Pipeline Construction Reference.

For complex cases, search the existing repository of sample applications for guidance.

If the user wants to add custom application logic, always check if this logic can be implemented using existing GStreamer elements or their combination. If it cannot, add a custom Python element to the pipeline and implement the logic there. Follow the [Custom Python Element Conventions](./references/coding-conventions.md#custom-python-element-conventions) for implementation details.

#### Reference Python Samples

Before generating code, read the relevant existing samples to understand established conventions:

| Sample | Key Pattern | Path |
|--------|-------------|------|
| hello_dlstreamer | Minimal pipeline + pad probe | `samples/gstreamer/python/hello_dlstreamer/` |
| face_detection_and_classification | Detect → classify chain, HuggingFace model export | `samples/gstreamer/python/face_detection_and_classification/` |
| prompted_detection | Third-party model integration (YOLOE), appsink callback | `samples/gstreamer/python/prompted_detection/` |
| open_close_valve | Dynamic pipeline control, tee + valve, OOP controller | `samples/gstreamer/python/open_close_valve/` |
| vlm_alerts | VLM inference (gvagenai), argparse config, file output | `samples/gstreamer/python/vlm_alerts/` |
| vlm_self_checkout | Computer Vision detection and VLM classification, multi-branch tee, custom frame selection for VLM | `samples/gstreamer/python/vlm_self_checkout/` |
| smart_nvr | Custom Python GStreamer elements (analytics + recorder), chunked storage | `samples/gstreamer/python/smart_nvr/` |
| onvif_cameras_discovery | Multi-camera RTSP, ONVIF discovery, subprocess orchestration | `samples/gstreamer/python/onvif_cameras_discovery/` |
| draw_face_attributes | Detect → multi-classify chain, custom tensor post-processing in pad probe callback | `samples/gstreamer/python/draw_face_attributes/` |
| coexistence | DL Streamer + DeepStream coexistence, Docker orchestration, multi-framework LPR | `samples/gstreamer/python/coexistence/` |

#### Reference Command Line Samples

Before generating code, read the relevant existing samples to understand established conventions:

| Sample | Key Pattern | Path |
|--------|-------------|------|
| face_detection_and_classification | Detection + classification chain (`gvadetect` → `gvaclassify`) | `samples/gstreamer/gst_launch/face_detection_and_classification/` |
| audio_detect | Audio event detection + metadata publish | `samples/gstreamer/gst_launch/audio_detect/` |
| audio_transcribe | Audio transcription with `gvaaudiotranscribe` | `samples/gstreamer/gst_launch/audio_transcribe/` |
| vehicle_pedestrian_tracking | Detection + tracking (`gvatrack`) | `samples/gstreamer/gst_launch/vehicle_pedestrian_tracking/` |
| human_pose_estimation | Full-frame pose estimation/classification | `samples/gstreamer/gst_launch/human_pose_estimation/` |
| metapublish | Metadata conversion and publish (`gvametaconvert`/`gvametapublish`) | `samples/gstreamer/gst_launch/metapublish/` |
| gvapython/face_detection_and_classification | Python post-processing via `gvapython` | `samples/gstreamer/gst_launch/gvapython/face_detection_and_classification/` |
| gvapython/save_frames_with_ROI_only | Save ROI frames with `gvapython` | `samples/gstreamer/gst_launch/gvapython/save_frames_with_ROI_only/` |
| action_recognition | Action recognition pipeline | `samples/gstreamer/gst_launch/action_recognition/` |
| instance_segmentation | Instance segmentation pipeline | `samples/gstreamer/gst_launch/instance_segmentation/` |
| detection_with_yolo | YOLO-based detection/classification | `samples/gstreamer/gst_launch/detection_with_yolo/` |
| geti_deployment | Intel® Geti™ model deployment | `samples/gstreamer/gst_launch/geti_deployment/` |
| multi_stream | Multi-camera / multi-stream processing | `samples/gstreamer/gst_launch/multi_stream/` |
| gvaattachroi | Attach custom ROIs before inference | `samples/gstreamer/gst_launch/gvaattachroi/` |
| gvafpsthrottle | FPS throttling with `gvafpsthrottle` | `samples/gstreamer/gst_launch/gvafpsthrottle/` |
| lvm | Image embeddings generation with ViT/CLIP | `samples/gstreamer/gst_launch/lvm/` |
| license_plate_recognition | License plate recognition (detector + OCR) | `samples/gstreamer/gst_launch/license_plate_recognition/` |
| gvagenai | VLM usage with `gvagenai` | `samples/gstreamer/gst_launch/gvagenai/` |
| g3dradarprocess | Radar signal processing | `samples/gstreamer/gst_launch/g3dradarprocess/` |
| g3dlidarparse | LiDAR parsing pipeline | `samples/gstreamer/gst_launch/g3dlidarparse/` |
| gvarealsense | RealSense camera capture | `samples/gstreamer/gst_launch/gvarealsense/` |
| custom_postproc/detect | Custom detection post-processing library | `samples/gstreamer/gst_launch/custom_postproc/detect/` |
| custom_postproc/classify | Custom classification post-processing library | `samples/gstreamer/gst_launch/custom_postproc/classify/` |
| face_detection_and_classification_bins | Detection + classification using `processbin`, GPU/CPU VA memory paths | `samples/gstreamer/gst_launch/face_detection_and_classification_bins/` |
| motion_detect | Motion region detection (`gvamotiondetect`), ROI-restricted inference | `samples/gstreamer/gst_launch/motion_detect/` |


### Step 5a [Command Line Application] — Construct Command Line Pipeline for Simple Use Cases

If the user asks for a command-line application, construct a `gst-launch-1.0` pipeline string using the identified DLStreamer elements. Follow established conventions for element properties, caps negotiation, and metadata handling as seen in the reference command line samples.

### Step 5b [Python Application] — Construct Python Applications for Complex Use Cases and Custom Application Logic

If the user asks for a Python application or wants to add custom logic as new Python elements, decompose the requested pipeline into one or more of the design patterns listed in the [Design Patterns Reference](./references/design-patterns.md). This will guide the structure of the application, including how to construct the pipeline, where to add callbacks, and how to handle models and metadata.

Map the user's description to one or more patterns using the [Pattern Selection Table](./references/design-patterns.md#pattern-selection-table) in the Design Patterns Reference.

Read the [Coding Conventions Reference](./references/coding-conventions.md) before writing a Python application.
Use the [Application Template](./assets/python-app-template.py) as a starting skeleton.

Compose the application by:
1. Selecting the appropriate **pipeline construction** approach — see [Pipeline Construction Reference](./references/pipeline-construction.md)
2. Following the **Pipeline Design Rules** (Rules 1–8) in the Pipeline Construction Reference — prefer auto-negotiation, GPU/NPU inference, `gvaclassify` for OCR, `gvametapublish` for JSON, multi-device assignment on Intel Core Ultra, fragmented MP4 for robustness (Rule 7), audio track handling (Rule 8)
3. Assembling the **pipeline string** from DLStreamer elements listed in the Pipeline Construction Reference
4. Preparing models using the correct export method — see [Model Preparation Reference](./references/model-preparation.md)
5. Adding **callbacks/probes** as needed
6. Adding **custom Python elements** if the user needs inline analytics
7. Wiring up **argument parsing** and **asset resolution**
8. Adding the **pipeline event loop** — see [Pattern 12: Pipeline Event Loop](./references/design-patterns.md#pattern-12-pipeline-event-loop)

### Step 6 — Generate Sample Application

Generate the sample application following the directory structure outlined at the beginning of this document.
Use the [README Template](./assets/README-template.md) to generate the `README.md` file — replace `{{PLACEHOLDERS}}` with application-specific content and remove HTML comments.

If the application requires Python dependencies, list them in `requirements.txt`, then create and activate a local Python environment before running the application. If the OpenVINO Python runtime is required, make sure it is added to `requirements.txt` with the same version as the OpenVINO runtime installed with DLStreamer.

```bash
source .<app_name>-venv/bin/activate
pip install -r requirements.txt
python3 <app_name>.py  # or bash <app_name>.sh
```

When running the application inside the container, add write access to the mounted directory as the sample will generate results there.
Use `-u "$(id -u):$(id -g)"` to run the container as the current user, or pre-create writable
output directories (`videos/`, `results/`, `models/`) before launching the container.
Also mount `/dev/dri` for media and GPU device drivers, as well as `/dev/accel` for NPU devices when available on the host system.
Note that the DLStreamer container does not come with render or accel group permissions by default, so you need to add them at runtime using the `--group-add` flag and the `stat` command to query the correct group ID for your system. For example:

```bash
docker run -it --rm \
    -u "$(id -u):$(id -g)" \
    -v "$(pwd)":/app -w /app \
    --device /dev/dri \
    --group-add $(stat -c "%g" /dev/dri/render*) \
    --device /dev/accel \
    --group-add $(stat -c "%g" /dev/accel/accel*) \
    intel/dlstreamer:<WEEKLY_TAG> \
    python3 <app_name>.py
```

Replace `<WEEKLY_TAG>` with the actual tag discovered in Step 3 (e.g. `2026.1.0-20260407-weekly-ubuntu24`).

### Step 7 — Run, Debug and Validate Application

Once the environment is set up, update the instructions in the generated README.md file and verify that the application runs correctly when following them. If the user provided a natural-language description of the expected output, verify that the output matches the description (e.g. check that JSONL files have the expected fields, check that video outputs have the expected overlays, etc.).

If the application is running for a long time (>1 minute), make sure there is some output in the terminal to indicate progress and avoid leaving the user wondering if the application is stuck. Switch focus to the terminal output so the user can see logs and progress.
If the application has a continuous input stream (RTSP camera source) or large input video files, send an EOS signal to the application.

Refer to the [Debugging Hints](./references/debugging-hints.md) for Docker testing conventions, common gotchas, and the post-run [Validation Checklist](./references/debugging-hints.md#validation-checklist).

### Step 8 — Report Coding Agent Analytics

Report the time taken for the entire activity, from prompt to working application. Report the following metrics:
1) AI reasoning time (time spent on understanding the prompt, designing the pipeline, writing code, etc.).
2) Environment setup time (time spent waiting for `pip install`, model export, Docker image pull, etc.).
3) Debug and Validation time (time spent running the application, checking outputs, and fixing issues).
4) Time waiting for user action (time spent waiting for user input or confirmation).
5) Total activity time (please note some phases may overlap, so the total time is not necessarily the sum of individual phases).
This will help us understand how much of the process is automated vs how much requires human input and waiting time.

## Examples
See [example prompts](./examples) for inspiration on how to write effective prompts for DLStreamer Coding Agent, and to see how the above procedure can be applied in practice to generate new sample applications.

