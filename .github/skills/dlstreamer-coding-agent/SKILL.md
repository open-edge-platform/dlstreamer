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

See [example prompts](/examples) for inspiration.

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

## Procedure

### Step 1 — Refine User Prompt

The User prompt may be ambiguous or incomplete. Before proceeding further make sure the following details are clarified:
1) Input source (video file vs RTSP stream, single vs multi-camera, etc.), ask for specific video file if possible
2) AI model types (detection, classification, OCR, VLM, etc.) and specific models if possible (e.g. "YOLOv8 for detection and PaddleOCRv5 for OCR)
If a User does not have specific models in mind, try to infer the most likely model choice based on the task description and list of models supported by DLStreamer (`../../../../docs/user-guide/supported_models.md`).
3) Sequence of operations in the pipeline (e.g. detection → tracking -> classification, or detection + VLM in parallel branches, etc.)
4) Expected output (e.g. JSON file with license plate text, annotated video file, etc.)
5) Performance requirements (e.g. real-time processing, batch processing, etc.)

### Step 2 — Create Model Download and Export Scripts

> **Parallelization hint:** Steps 2 and 3 are independent and both involve large network
> downloads. Start them in parallel — e.g. kick off model export in one terminal while
> pulling the Docker image in another.

Check what AI models a User wants to use. Search if the requested or similar models are in the list of models supported by DLStreamer

| Model exporter | Typical Models  | Path |
|--------|-------------|------|
| download_public_models.sh | Traditional computer vision models | `samples/download_public_models.sh` |
| download_hf_models.py | HuggingFace models, including VLM models and Transformer-based detection/classification models (RTDETR, CLIP, ViT) | `scripts/download_models/download_hf_models.py` |
| download_ultralytics_models.py | Specialized model downloader for Ultralytics YOLO models | `scripts/download_models/download_ultralytics_models.py` |

If a model is found in one of the above scripts, extract model download recipe from that script and create a local script in application directory for exporting the specific model to OV IR format; add model export instructions to the application README.
If a model does not exist, check the [Model Preparation Reference](./references/model-preparation.md) for instructions on how to prepare and export the model for DLStreamer, then write a new model download/export script using the [Export Models Template](./assets/export-models-template.py) as a starting point and add instructions to the application README.

Create the `export_requirements.txt` file if the model export script requires additional Python packages (e.g. HuggingFace transformers, Ultralytics, optimum-cli, etc.). Add comments in `export_requirements.txt` to indicate which model export script requires a specific package. Use specific version numbers for packages to ensure reproducibility.

Run the model export script to verify that the models can be downloaded and exported correctly to OpenVINO IR format.
Create and set up a Python virtual environment to isolate dependencies:

```bash
python3 -m venv .<app_name>-export-venv
source .<app_name>-export-venv/bin/activate
pip install -r export_requirements.txt
python3 export_models.py  # or bash export_models.sh
```

> **Important:** When running terminal commands that may take a long time (e.g. `pip install`,
> model downloads, model export), do **not** pipe output through `tail`, `head`, or other
> filters that hide progress. Let the full output stream to the terminal so the user can
> see download/install progress and is not left waiting with no feedback.

### Step 3 — Check and Setup Deployment Environment

Check if the user machine has DLStreamer installed:
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

***Important*** - While DLStreamer Coding Agent is still in preview version, ALWAYS download the latest weekly build even if a User has latest official version of DLStreamer installed, as the latest weekly build will contain important bug fixes and improvements that are not yet in the official release.

Recommended workflow: develop the application locally on your host machine and prepare/export models using a Python virtual environment. Once models are exported to OpenVINO IR format, run the application inside the DLStreamer container with your local directory mounted. This approach maintains development flexibility while leveraging the container for consistent runtime execution.

### Step 4 — Define DLStreamer Pipeline from User Description

Generate a DLStreamer pipeline string that captures the user's intent using DLStreamer elements. Use the [Pipeline Construction Reference](./references/pipeline-construction.md) to identify which elements to use for each part of the pipeline (e.g. source, decode, inference, metadata handling, sink).

For common use cases, go straight to file generation using the [use-case → template/pattern mapping table](./references/pipeline-construction.md#common-pipeline-patterns) in the Pipeline Construction Reference.

For complex cases, search existing repository of sample applications for guidance.

If a User wants to add custom application logic, always check if this logic can be implemented using existing GStreamer elements or their combination. If it  cannot, add a custom Python element to the pipeline and implement the logic there. Follow the [Custom Python Element Conventions](./references/coding-conventions.md#custom-python-element-conventions) for implementation details.

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

Read the [Coding Conventions Reference](./references/coding-conventions.md) before writing a Python application.
Use the [Application Template](./assets/python-app-template.py) as a starting skeleton. Compose the application by:

1. Selecting the appropriate **pipeline construction** approach — see [Pipeline Construction Reference](./references/pipeline-construction.md)
2. Following the **Pipeline Design Rules** (Rules 1–6) in the Pipeline Construction Reference — prefer auto-negotiation, GPU/NPU inference, `gvaclassify` for OCR, `gvametapublish` for JSON, multi-device assignment on Intel Core Ultra
3. Assembling the **pipeline string** from DLStreamer elements listed in the Pipeline Construction Reference
4. Preparing models using the correct export method — see [Model Preparation Reference](./references/model-preparation.md)
5. Adding **callbacks/probes** as needed
6. Adding **custom Python elements** if the user needs inline analytics
7. Wiring up **argument parsing** and **asset resolution**
8. Adding the **pipeline event loop**


### Step 6 — Generate Sample Application

Generate sample application following the directory structure outlined at the beginning of this document.
Use the [README Template](./assets/README-template.md) to generate the `README.md` file — replace `{{PLACEHOLDERS}}` with application-specific content and remove HTML comments.

If an application requires Python dependencies, list them in `requirements.txt` and then create and activate a local Python environment prior to running the application. If OpenVINO python runtime is required, please make sure it is added to `requirements.txt` with same version as OpenVINO runtime installed with DLStreamer.

```bash
source .<app_name>-venv/bin/activate
pip install -r requirements.txt
python3 <app_name>.py  # or bash <app_name>.sh
```

When running the application inside the container, add write access to the mounted directory as the sample will generate results there.
Use `-u "$(id -u):$(id -g)"` to run the container as the current user, or pre-create writable
output directories (`videos/`, `results/`, `models/`) before launching the container.
Mount also `/dev/dri` for Media and GPU device drivers as well as `/dev/accel` for NPU devices when available in the host system.
Note DLStreamer container does not come with render or accel group permissions by default, so you need to add them at runtime using `--group-add` flag and `stat` command to query the correct group ID for your system. For example:

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

> **Known Docker warnings:** GStreamer plugin scanner may emit warnings about Python symbol
> loading (e.g. `undefined symbol: PyExc_ValueError`) when running inside the DLStreamer
> Docker container. These are harmless and do not affect pipeline execution.
```

Once the environment is set up, update instructions in generated README.md file and verify the application runs correctly when following instructions. If the user provided a natural language description of the expected output, verify that the output matches the description (e.g. check that JSONL files have the expected fields, check that video outputs have the expected overlays, etc.).


## Examples
See [example prompts](/examples) for inspiration on how to write effective prompts for DLStreamer Coding Agent, and to see how the above procedure can be applied in practice to generate new sample applications.

