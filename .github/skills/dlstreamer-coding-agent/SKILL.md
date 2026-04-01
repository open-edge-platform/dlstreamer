---
name: dlstreamer-coding-agent
description: "Build new DLStreamer Python video-analytics applications. Use when: user describes a vision AI pipeline, wants to create a new sample app, combine elements from existing samples, add detection/classification/VLM/tracking/alerts/recording to a video pipeline, or create custom GStreamer elements in Python. Translates natural-language pipeline descriptions into working DLStreamer Python code using established design patterns."
argument-hint: "Describe the vision AI pipeline you want to build (e.g. 'detect faces in RTSP stream and save alerts as JSON')"
---

# DLStreamer Coding Agent

Build new DLStreamer Python video-analytics applications by composing design patterns extracted from existing sample apps.

## When to Use

- User describes a vision AI processing pipeline in natural language
- User wants to create a new sample app derived from existing samples
- User wants to combine elements from multiple existing samples (e.g. detection + VLM + recording)
- User needs to add custom analytics logic or custom GStreamer elements in Python

## Reference Samples

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
| license_plate_recognition | Detect + custom OpenVINO element, PaddlePaddle model conversion, pixel access in custom element | `samples/gstreamer/python/license_plate_recognition/` |
| onvif_cameras_discovery | Multi-camera RTSP, ONVIF discovery, subprocess orchestration | `samples/gstreamer/python/onvif_cameras_discovery/` |

## Procedure

### Step 1 — Decompose the User Request into Design Patterns

Read the [Design Patterns Reference](./references/design-patterns.md) first.

Map the user's description to one or more of these patterns:

| Pattern | When to Apply |
|---------|---------------|
| **Pipeline Core** | Always — every app needs source → decode → sink |
| **AI Inference** | User wants object detection (`gvadetect`), classification/OCR (`gvaclassify`), or VLM (`gvagenai`) |
| **Pad Probe Callback** | User needs per-frame metadata inspection or custom overlays |
| **AppSink Callback** | User wants to pull frames into Python for custom processing |
| **Dynamic Pipeline Control** | User wants conditional routing, valve, or tee-based branching |
| **Custom Python Element** | User needs custom analytics logic that runs inside the pipeline |
| **Cross-Branch Signal Bridge** | User has a tee with branches that must exchange state |
| **Model Download & Export** | User references HuggingFace, Ultralytics, or optimum-cli models |
| **Asset Resolution** | User expects auto-download of video or model files |
| **Multi-Camera / RTSP** | User wants to process multiple camera streams |
| **File Output (gvametapublish)** | User wants to save JSONL results — use `gvametapublish file-format=json-lines` as default |
| **Custom OpenVINO Inference** | User needs a model not supported by `gvaclassify` (fallback only — prefer `gvaclassify` with model-proc first) |
| **Pixel Access in Custom Element** | Custom element needs to read/crop raw frame pixels (not just metadata) |

### Step 1.5 — Verify Model Details Before Writing Code

Before writing any model download/export code, **always verify HuggingFace repo contents first**:

```python
from huggingface_hub import list_repo_files
files = list_repo_files("owner/model-name")
```

This avoids the common mistake of assuming filenames (e.g. guessing `best.pt` when the
actual file is `license-plate-finetune-v1s.pt`). Also read the [Common Pitfalls Reference](./references/common-pitfalls.md) to avoid known failure modes.

### Step 2 — Read Relevant Sample Code

Based on the patterns identified, read the actual source files from the samples listed above. Do NOT generate code from memory — always read the current source to pick up the latest API conventions and imports.

For each pattern needed, read the corresponding sample file(s) listed in [Design Patterns Reference](./references/design-patterns.md).

### Step 3 — Assemble the Application

Use the [Application Template](./assets/app-template.py) as a starting skeleton. Compose the application by:

1. Selecting the appropriate **pipeline construction** approach — see [Pipeline Construction Reference](./references/pipeline-construction.md)
2. Following the **Pipeline Design Rules** (Rules 1–4) in the Pipeline Construction Reference — prefer auto-negotiation, GPU/NPU inference, `gvaclassify` for OCR, `gvametapublish` for JSON
3. Assembling the **pipeline string** from DLStreamer elements listed in the Pipeline Construction Reference
3. Preparing models using the correct export method — see [Model Preparation Reference](./references/model-preparation.md)
4. Adding **callbacks/probes** as needed
5. Adding **custom Python elements** if the user needs inline analytics
6. Wiring up **argument parsing** and **asset resolution**
7. Adding the **pipeline event loop**

**Review [Common Pitfalls Reference](./references/common-pitfalls.md)** before finalizing the code — it lists concrete mistakes and fixes discovered during development of previous samples.

### Step 4 — Follow Project Conventions

Read the [Coding Conventions Reference](./references/coding-conventions.md) and ensure:

- Copyright header is present
- Imports follow the `gi.require_version` pattern
- Pipeline loop uses `bus.timed_pop_filtered` for EOS/ERROR
- Custom elements follow the `GstBase.BaseTransform` or `Gst.Bin` pattern
- argparse is used for non-trivial apps; sys.argv for simple ones
- Results go to a `results/` subdirectory

### Step 5 — Generate Supporting Files

For each new sample app, generate:
- `<app_name>.py` — main application
- `README.md` — description, pipeline diagram, running instructions
- `requirements.txt` — Python dependencies (if any beyond PyGObject)
- `plugins/python/<element_name>.py` — for any custom GStreamer elements
