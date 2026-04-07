---
name: dlstreamer-coding-agent
description: "Build new DLStreamer Python video-analytics applications. Use when: user describes a vision AI pipeline, wants to create a new sample app, combine elements from existing samples, add detection/classification/VLM/tracking/alerts/recording to a video pipeline, or create custom GStreamer elements in Python. Translates natural-language pipeline descriptions into working DLStreamer Python code using established design patterns."
argument-hint: "Describe the vision AI pipeline you want to build (e.g. 'detect faces in RTSP stream and save alerts as JSON')"
---

# DLStreamer Coding Agent

Build new DLStreamer Python video-analytics applications by composing design patterns extracted from existing sample apps.

## When to Use

- User describes a vision AI processing pipeline in natural language
- User wants to create a new Python sample application built on DLStreamer
- User wants to create a new GStreamer command line using DLStreamer elements
- User wants to combine elements from multiple existing samples (e.g. detection + VLM + recording)
- User needs to add custom analytics logic or custom GStreamer elements in Python

##  Directory Layout for a New Sample App

```
<new_sample_app_name>
├── <app_name>.py or .sh        # Main application (Python or shell script)
├── <download_models.py or .sh  # Model download script (if not embedded in the main application)
├── README.md                   # Documentation with instructions how to install prerequisites and run the sample
├── requirements.txt            # Python dependencies (if any, including PyGObject)
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

### Step 1 — Identify AI Models and Generate Model Download scripts

Check what AI models a User wants to use. Search if the models are in the list of models supported by DLStreamer

| Model downloader | Typical Models  | Path |
|--------|-------------|------|
| download_public_models.sh | Traditional computer vision models | `samples/download_public_models.sh` |
| download_hf_models.py | HuggingFace models, including VLM models and Transformer-based detection/classification models (RTDETR, CLIP, ViT) | `scripts/download_models/download_hf_models.py` |
| download_ultralytics_hf_models.py | Specialized model downloader for Ultralytics YOLO models | `scripts/download_models/download_ultralytics_models.py` |

If a model is found in one of the above scripts, extract model download receipie from that script and create a local script in application directory for downloading the specific model; add model download instructions to the application README.
If a model does not exist, check the [Model Preparation Reference](./references/model-preparation.md) for instructions on how to prepare and export the model for DLStreamer, then write a new model download/export script in the application repository and add instructions to the application README.

Add dependencies to `requirements.txt` if the model download script requires additional Python packages (e.g. HuggingFace transformers, Ultralytics, optimum-cli, etc.). Add comments in `requirements.txt` to indicate which model downloader script requires a specific package. Use specific version numbers for packages to ensure reproducibility.

Run the model download script to verify that the models can be downloaded and exported correctly to OpenVINO IR format. 
Create and set up a Python virtual environment to isolate dependencies:

```bash
python3 -m venv .<app_name>-venv
source .<app_name>-venv/bin/activate
pip install -r requirements.txt
python3 download_models.py  # or bash download_models.sh
```

### Step 2 — Define DLStreamer Pipeline from User Description

Generate a DLStreamer pipeline string that captures the user's intent using DLStreamer elements. Use the [Pipeline Construction Reference](./references/pipeline-construction.md) to identify which elements to use for each part of the pipeline (e.g. source, decode, inference, metadata handling, sink).

<!-- To avoid managing DLStreamer dependencies, it is recommended to download DLStreamer docker image and run the application inside the container. This will ensure that the correct version of DLStreamer and OpenVINO runtime are used, and that all necessary GStreamer plugins are available. Refer to the [DLStreamer Install Guide](docs/user-guide/get_started/get_started_index.md) for instructions on how to set up and run applications inside the DLStreamer container. -->

### Step 3a [Command Line Application] — Construct Command Line Pipeline

If the user asks for a command-line application, construct a `gst-launch-1.0` pipeline string using the identified DLStreamer elements. Follow established conventions for element properties, caps negotiation, and metadata handling as seen in the reference command line samples.

### Step 3b [Python Application] — Decompose the User Request into Design Patterns

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

### Step 4 [Python Application] — Assemble the Application

Read the [Coding Conventions Reference](./references/coding-conventions.md) before writing a Python application.
Use the [Application Template](./assets/python-app-template.py) as a starting skeleton. Compose the application by:

1. Selecting the appropriate **pipeline construction** approach — see [Pipeline Construction Reference](./references/pipeline-construction.md)
2. Following the **Pipeline Design Rules** (Rules 1–5) in the Pipeline Construction Reference — prefer auto-negotiation, GPU/NPU inference, `gvaclassify` for OCR, `gvametapublish` for JSON
3. Assembling the **pipeline string** from DLStreamer elements listed in the Pipeline Construction Reference
4. Preparing models using the correct export method — see [Model Preparation Reference](./references/model-preparation.md)
5. Adding **callbacks/probes** as needed
6. Adding **custom Python elements** if the user needs inline analytics
7. Wiring up **argument parsing** and **asset resolution**
8. Adding the **pipeline event loop**

### Step 5 — Generate Sample Application

Generate sample application following the directory structure outlined at the beginning of this document.

If an application requires Python dependencies, list them in `requirements.txt` and then create and activate a local Python environment prior to running the application. If OpenVINO python runtime is required, please make sure it is added to `requirements.txt` with same version as OpenVINO runtime installed with DLStreamer.

```bash
source .<app_name>-venv/bin/activate
pip install -r requirements.txt
python3 <app_name>.py  # or bash <app_name>.sh
```

Once the environment is set up, follow instructions in generated README.md file and verify the application runs correctly with the generated code. If the user provided a natural language description of the expected output, verify that the output matches the description (e.g. check that JSONL files have the expected fields, check that video outputs have the expected overlays, etc.).


