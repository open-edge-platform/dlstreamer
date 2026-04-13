---
name: dlstreamer-coding-agent
description: "Build new DL Streamer video-analytics applications (Python or gst-launch command line). Use when: user describes a vision AI pipeline, wants to create a new sample app, combine elements from existing samples, add detection/classification/VLM/tracking/alerts/recording to a video pipeline, or create custom GStreamer elements in Python. Gathers pipeline requirements interactively — input sources, AI models (URLs or names), target Intel hardware, expected outputs — then generates working DL Streamer code using established design patterns."
argument-hint: "Describe the vision AI pipeline you want to build (e.g. 'detect faces in RTSP stream and save alerts as JSON')"
---

# DL Streamer Coding Agent

Build new DL Streamer video-analytics applications (Python or gst-launch command line) by composing design patterns extracted from existing sample apps.

## When to Use

- User describes a vision AI processing pipeline in natural language
- User wants to create a new Python sample application built on DL Streamer
- User wants to create a new GStreamer command line using DL Streamer elements
- User wants to combine elements from multiple existing samples (e.g. detection + VLM + recording)
- User needs to add custom analytics logic or custom GStreamer elements in Python

## Directory Layout for a New Sample App

```
<new_sample_app_name>
├── <app_name>.py or .sh        # Main application (Python or shell script)
├── export_models.py or .sh     # Model download and export script
├── requirements.txt            # Python dependencies for the application
├── export_requirements.txt     # Python dependencies for model export scripts
├── README.md                   # Documentation with instructions how to install prerequisites and run the sample
├── plugins/                    # Only if custom GStreamer elements are needed
│   └── python/
│       └── <element>.py
├── config/                     # Only if config files are needed
│   └── *.txt / *.json
├── models/                     # Created at runtime (cached model exports)
├── videos/                     # Created at runtime (cached video downloads)
└── results/                    # Created at runtime (output files)
```

## Reference Python Samples

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
## Reference Command Line Samples

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

## Procedure

### Step 0 — Gather Requirements Interactively

Before generating any code, use the `vscode_askQuestions` tool to collect pipeline requirements
from the user. This ensures the generated application matches their exact needs.

Pre-fill options based on the user's initial prompt where possible, but always present
the questions for explicit confirmation. **Always present all questions, even if the
user's prompt implies answers** — the user must explicitly confirm or adjust.

Use a **single** `vscode_askQuestions` call with all of the following questions:

#### Section 1 — Input

| Question Header | Question | Options (if applicable) |
|----------------|----------|------------------------|
| `Input Type` | What type of video input will you use? | `Local file path`, `HTTP URL`, `RTSP stream URI` |
| `Input Value` | Provide the video input path or URL (e.g. `/path/to/video.mp4`, `https://...`, `rtsp://...`) | Free text |

#### Section 2 — AI Models

Pre-fill the `Model URLs` field with recommendations from the known-good models table
when the use case is clear, but still ask for confirmation.

| Question Header | Question | Options (if applicable) |
|----------------|----------|------------------------|
| `Model 1` | Provide the first model URL or name and its role (e.g. `yolo11n — vehicle detection`). Pre-filled with a recommendation when the use case is clear. | Free text (pre-fill from known-good models table when use case is clear) |
| `Model 2 (optional)` | If you need a second model, provide its URL or name and role (e.g. `paddlepaddle/PaddleOCR-rec-en — plate text OCR`). Leave empty if not needed. | Free text (pre-fill if use case needs multiple models, e.g. detection + OCR) |

> Add more `Model N` questions only if the use case clearly requires 3+ models.
> The agent infers the source ecosystem (HuggingFace / Ultralytics / direct URL) and
> the pipeline task (detection / classification / OCR / VLM / segmentation) automatically
> from the model URL or name — no need to ask the user separately.

> **Model URL handling:** When the user provides URLs, determine the source ecosystem:
> - `huggingface.co/<org>/<model>` → Extract repo ID, use `optimum-cli` or `huggingface_hub` download
> - Ultralytics model names (e.g. `yolo11n`, `yoloe-26s-seg`) → Use `ultralytics` Python API
> - Direct `.onnx` / `.pt` / `.tflite` URLs → Use the universal conversion path in [Model Preparation Reference](./references/model-preparation.md) § 8
> - If the user says "recommend a model", suggest from the known-good models list below

**Known-good models for common tasks:**

| Task | Recommended Model | Source |
|------|-------------------|--------|
| General object detection | `yolo11n` or `yolo11s` | Ultralytics |
| Face detection | `arnabdhar/YOLOv8-Face-Detection` | HuggingFace (Ultralytics) |
| Person/vehicle detection | `yolo11n` | Ultralytics |
| Image classification | `dima806/fairface_age_image_detection` | HuggingFace (optimum-cli) |
| OCR / text recognition | `paddlepaddle/PaddleOCR-rec-en` | HuggingFace (PaddlePaddle) |
| VLM scene description | `OpenGVLab/InternVL3_5-2B` | HuggingFace (optimum-cli) |
| VLM alerting (small) | `HuggingFaceTB/SmolVLM2-2.2B-Instruct` | HuggingFace (optimum-cli) |
| Open-vocabulary detection | `yoloe-26s-seg` | Ultralytics |
| License plate detection | `yolo11n` + PaddleOCR | Ultralytics + HuggingFace |

#### Section 3 — Target Hardware & Optimization

| Question Header | Question | Options (if applicable) |
|----------------|----------|------------------------|
| `Intel Platform` | What Intel hardware will this run on? | `Intel Core Ultra (Meteor Lake) — CPU + GPU + NPU`, `Intel Core Ultra (Lunar Lake / Arrow Lake) — CPU + GPU + NPU`, `Intel Core Ultra (Panther Lake) — CPU + Xe3 GPU + NPU 5`, `Intel Xeon (server) — CPU only`, `Intel Arc discrete GPU`, `Intel Core (older, no NPU) — CPU + GPU`, `Not sure / detect at runtime` |
| `Available Accelerators` | Which accelerators are available? | `GPU (/dev/dri/renderD128)`, `NPU (/dev/accel/accel0)`, `CPU only` (multiSelect) |
| `Optimization Priority` | What matters most? | `Maximum throughput (FPS)`, `Lowest latency per frame`, `Power efficiency (NPU preferred)`, `Balanced (default)` |

> **After gathering answers**, read the [Hardware Optimization Reference](./references/hardware-optimization.md)
> to map the user's platform and priorities to specific `device=` settings, `batch-size` values,
> and weight format choices for model export.

#### Section 4 — Output

| Question Header | Question | Options (if applicable) |
|----------------|----------|------------------------|
| `Output Format` | What outputs do you need? | `Annotated video (.mp4)`, `JSON metadata (.jsonl)`, `JPEG snapshots`, `Display window`, `All of the above` (multiSelect) |

If the user's initial prompt already implies answers (e.g. "detect faces using YOLOv8 on
RTSP camera with GPU"), pre-select the matching options as `recommended` but still present
all questions so the user can confirm or adjust.

#### Section 5 — Application Type

| Question Header | Question | Options (if applicable) |
| `Application Type` | Python application or gst-launch command line? | `Python application` (recommended), `gst-launch command line` |

#### Section 6 — Docker Image

| Question Header | Question | Options (if applicable) |
|----------------|----------|------------------------|
| `Docker Image` | Which DL Streamer Docker image to use? | See table below (multiSelect=false, allowFreeformInput=true) |

Present these options grouped by category. The agent should use the selected image
in all `docker pull` and `docker run` commands in the generated README and when
running the application for verification.

**Fetching available images at runtime:**

Before presenting the Docker Image question, fetch the latest tags from Docker Hub
by running this command in a terminal:

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
print('RELEASE:', ' '.join(release[:6]))
print('WEEKLY:', ' '.join(weekly[:4]))
"
```

Use the output to populate the `Docker Image` question options dynamically:

1. Always include `intel/dlstreamer:latest` as the first option (recommended)
2. Add the top 3 **release** tags from the `RELEASE` line (e.g. `2026.0.0-ubuntu24`, `2026.0.0-ubuntu22`, `2025.2.0-ubuntu24`)
3. Add the top 2 **weekly** tags from the `WEEKLY` line (e.g. `2026.1.0-20260407-weekly-ubuntu24`, `2026.1.0-20260407-weekly-ubuntu22`)

Format each option label as `intel/dlstreamer:<tag>` with a short description derived
from the tag (Ubuntu version, release/weekly, date).

> The user can also type a custom image tag (e.g. a locally built image or a
> specific RC tag like `intel/dlstreamer:2026.0.0-ubuntu24-rc3`).
> Full list of available tags: https://hub.docker.com/r/intel/dlstreamer/tags


### Quick Recipes

For common use cases, skip reading reference samples and go straight to file generation
using these recipes:

| Use Case | Templates | Design Patterns | Key Model Export |
|----------|-----------|-----------------|------------------|
| Detection + save video + JSON | `python-app-template.py` | 1 + 4 + 11 | Ultralytics |
| Detection + classification/OCR + save | `python-app-template.py` + `export-models-template.py` | 1 + 4 + 11 + 13 | YOLO + PaddleOCR/optimum-cli |
| VLM alerting + save | `python-app-template.py` | 1 + 9 + 11 | optimum-cli |
| Detection + custom analytics | `python-app-template.py` | 1 + 4 + 6 + 11 | Ultralytics |
| Detection + tracking + recording | `python-app-template.py` | 1 + 4 + 5 + 7 | Ultralytics |
| Multi-camera RTSP | `python-app-template.py` | 1 + 12 | (per camera) |

For use cases matching a recipe above, generate all files directly from the templates
and model-preparation reference without reading existing samples. Only read reference
samples when the use case doesn't match any recipe or requires unusual element combinations.

### Step 1 — Create Model Download and Export scripts

Based on the model information gathered in Step 0, determine the export path for each model.

**If the user provided model URLs or IDs**, classify each by source ecosystem:

| URL / ID Pattern | Ecosystem | Export Method |
|-----------------|-----------|---------------|
| `huggingface.co/<org>/<model>` or bare HF ID | HuggingFace | `optimum-cli export openvino` or `huggingface_hub` + Ultralytics |
| Ultralytics name (`yolo11n`, `yoloe-*`) | Ultralytics | `model.export(format="openvino")` |
| Direct URL ending in `.onnx` | ONNX | `ovc` directly |
| Direct URL ending in `.pt` / `.pth` | PyTorch | `torch.onnx.export` → `ovc` |
| Direct URL ending in `.tflite` | TensorFlow Lite | `ovc` directly |
| Direct URL to SavedModel `.tar.gz` / dir | TensorFlow | `ovc` directly |

For non-native models (ONNX, PyTorch, TF, etc.), follow the universal conversion path in
[Model Preparation Reference](./references/model-preparation.md) § 8.

**If the user did not provide URLs**, search if the requested or similar models are in the list of models supported by DL Streamer:

| Model exporter | Typical Models  | Path |
|--------|-------------|------|
| download_public_models.sh | Traditional computer vision models | `samples/download_public_models.sh` |
| download_hf_models.py | HuggingFace models, including VLM models and Transformer-based detection/classification models (RTDETR, CLIP, ViT) | `scripts/download_models/download_hf_models.py` |
| download_ultralytics_models.py | Specialized model downloader for Ultralytics YOLO models | `scripts/download_models/download_ultralytics_models.py` |

If a model is found in one of the above scripts, extract model download recipe from that script and create a local script in application directory for exporting the specific model to OV IR format; add model export instructions to the application README.
If a model does not exist, check the [Model Preparation Reference](./references/model-preparation.md) for instructions on how to prepare and export the model for DL Streamer, then write a new model download/export script using the [Export Models Template](./assets/export-models-template.py) as a starting point and add instructions to the application README.

Create the `export_requirements.txt` file if the model export script requires additional Python packages (e.g. HuggingFace transformers, Ultralytics, optimum-cli, etc.). Add comments in `export_requirements.txt` to indicate which model export script requires a specific package. Use specific version numbers for packages to ensure reproducibility.

Run the model export script to verify that the models can be downloaded and exported correctly to OpenVINO IR format. 
Create and set up a Python virtual environment to isolate dependencies:

```bash
python3 -m venv .<app_name>-export-venv
source .<app_name>-export-venv/bin/activate
pip install -r export_requirements.txt
python3 export_models.py  # or bash export_models.sh
```

### Step 2 — Define DL Streamer Pipeline from User Description

Generate a DL Streamer pipeline string that captures the user's intent using DL Streamer elements. Use the [Pipeline Construction Reference](./references/pipeline-construction.md) to identify which elements to use for each part of the pipeline (e.g. source, decode, inference, metadata handling, sink).

**Apply hardware-aware device assignment** based on the target platform gathered in Step 0:
- Read the [Hardware Optimization Reference](./references/hardware-optimization.md) for device mapping
- Set `device=` property on each inference element (`gvadetect`, `gvaclassify`, `gvagenai`) according to the user's available accelerators and optimization priority
- Set `batch-size=` based on the target device (GPU: 4–8, NPU: 1–2, CPU: 1–4)
- For multi-model pipelines, distribute inference across GPU and NPU to balance load
- Add runtime device detection fallback if the user selected "detect at runtime"

### Step 2a [Command Line Application] — Construct Command Line Pipeline

If the user asks for a command-line application, construct a `gst-launch-1.0` pipeline string using the identified DL Streamer elements. Follow established conventions for element properties, caps negotiation, and metadata handling as seen in the reference command line samples.

### Step 2b [Python Application] — Decompose the User Request into Design Patterns

If the user asks for a Python application or wants to add custom logic as new Python elements, decompose the requested pipeline into one or more of the design patterns listed in the [Design Patterns Reference](./references/design-patterns.md). This will guide the structure of the application, including how to construct the pipeline, where to add callbacks, and how to handle models and metadata.

Map the user's description to one or more of these patterns:

| Pattern | When to Apply |
|---------|---------------|
| **Pipeline Core** | Always — every app needs source → decode → sink |
| **AI Inference** | User wants object detection (`gvadetect`), classification/OCR (`gvaclassify`), or VLM (`gvagenai`) |
| **Pad Probe Callback** | User needs simple custom logic, like per-frame metadata inspection or adding overlays |
| **Custom Python Element** | User needs non-trivial custom analytics logic that runs inside the pipeline |
| **AppSink Callback** | User wants to continue processing of frames or metadata in their own application |
| **Dynamic Pipeline Control** | User wants conditional routing, valve, or tee-based branching |
| **Cross-Branch Signal Bridge** | User has a tee with branches that must exchange state |
| **Model Download & Export** | User references HuggingFace, Ultralytics, or optimum-cli models |
| **Asset Resolution** | User expects auto-download of video or model files |
| **Multi-Camera / RTSP** | User wants to process multiple camera streams |
| **File Output (gvametapublish)** | User wants to save JSONL results — use `gvametapublish file-format=json-lines` as default |

### Step 3 [Python Application] — Assemble the Application

Read the [Coding Conventions Reference](./references/coding-conventions.md) before writing a Python application.
Use the [Application Template](./assets/python-app-template.py) as a starting skeleton. Compose the application by:

1. Selecting the appropriate **pipeline construction** approach — see [Pipeline Construction Reference](./references/pipeline-construction.md)
2. Following the **Pipeline Design Rules** (Rules 1–5) in the Pipeline Construction Reference — prefer auto-negotiation, GPU/NPU inference, `gvaclassify` for OCR, `gvametapublish` for JSON
3. Assembling the **pipeline string** from DL Streamer elements listed in the Pipeline Construction Reference
4. Applying **hardware-aware device assignment** from [Hardware Optimization Reference](./references/hardware-optimization.md) — set `device=`, `batch-size=`, and weight format per the user's target platform
5. Preparing models using the correct export method — see [Model Preparation Reference](./references/model-preparation.md); for user-provided URLs or non-native formats, use § 8 (Universal Conversion)
6. Adding **callbacks/probes** as needed
7. Adding **custom Python elements** if the user needs inline analytics
8. Wiring up **argument parsing** and **asset resolution** — include `--device` argument with smart default based on target hardware
9. Adding the **pipeline event loop**

### Step 4 — Generate Sample Application

Generate sample application following the directory structure outlined at the beginning of this document.
Use the [README Template](./assets/README-template.md) to generate the `README.md` file — replace `{{PLACEHOLDERS}}` with application-specific content and remove HTML comments.

If an application requires Python dependencies, list them in `requirements.txt` and then create and activate a local Python environment prior to running the application. If OpenVINO python runtime is required, please make sure it is added to `requirements.txt` with same version as OpenVINO runtime installed with DL Streamer.

```bash
source .<app_name>-venv/bin/activate
pip install -r requirements.txt
python3 <app_name>.py  # or bash <app_name>.sh
```

Recommended workflow: develop the application locally on your host machine and prepare/export models using a Python virtual environment. Once models are exported to OpenVINO IR format, run the application inside the DL Streamer container with your local directory mounted. This approach maintains development flexibility while leveraging the container for consistent runtime execution.

**Running in the DL Streamer container** — use the Docker image the user selected in
Step 0 Section 6 (`<DOCKER_IMAGE>`, default `intel/dlstreamer:latest`). Always pull first.
Mount `/dev/dri` (GPU) and `/dev/accel` (NPU, when present). The container lacks render/accel
group membership, so add them via `--group-add` + `stat`. Use `-u` to preserve file ownership.

```bash
docker pull <DOCKER_IMAGE>

docker run -it --rm \
    -u "$(id -u):$(id -g)" \
    -v "$(pwd)":/app -w /app \
    --device /dev/dri \
    --group-add $(stat -c "%g" /dev/dri/render*) \
    --device /dev/accel \
    --group-add $(stat -c "%g" /dev/accel/accel*) \
    <DOCKER_IMAGE> \
    python3 <app_name>.py
```

Omit the `--device /dev/accel` and its `--group-add` line when NPU is not available on the host.

Once the environment is set up, update instructions in generated README.md file and verify the application runs correctly when following instructions. If the user provided a natural language description of the expected output, verify that the output matches the description (e.g. check that JSONL files have the expected fields, check that video outputs have the expected overlays, etc.).


