# Model Preparation Reference

DLStreamer inference elements (`gvadetect`, `gvaclassify`, `gvagenai`) consume models in
**OpenVINO IR format** (`.xml` + `.bin`). Source models come from three ecosystems; each has
a different download-and-export path.

## Model Sources and Export Methods

### 1. Ultralytics YOLO Models (detection / segmentation)

**When to use:** User asks for object detection, segmentation, or open-vocabulary detection
with YOLO, YOLOv8, YOLO11, YOLOE, or YOLO26.

**Export pattern — in-process (simple apps):**

```python
from ultralytics import YOLO

model = YOLO("yolo11n.pt")                                     # download weights
path  = model.export(format="openvino", dynamic=True, int8=True) # export to OV IR
model_file = f"{path}/yolo11n.xml"
```

Source: `samples/gstreamer/python/face_detection_and_classification/face_detection_and_classification.py`

**Export pattern — subprocess (when DLStreamer is already loaded):**

Ultralytics export creates a new OpenVINO runtime instance that can clash with DLStreamer's
runtime. For apps that also run a GStreamer pipeline in the same process, export in a
**separate subprocess**:

```python
import subprocess, sys
result = subprocess.run(
    [sys.executable, "export_yolo.py", "--model", "yolo26s.pt",
     "--outdir", str(MODELS_DIR), "--int8"],
    check=False
)
```

Source: `samples/gstreamer/python/vlm_self_checkout/vlm_self_checkout.py`

**Open-vocabulary detection (YOLOE) — prompt-based class selection:**

```python
from ultralytics import YOLO

model = YOLO("yoloe-26s-seg.pt")
names = ["white car"]
model.set_classes(names, model.get_text_pe(names))
path = model.export(format="openvino", dynamic=True, half=True)
model_file = f"{path}/yoloe-26s-seg.xml"
```

Source: `samples/gstreamer/python/prompted_detection/prompted_detection.py`

### 2. HuggingFace Transformer Models (classification / VLM)

**When to use:** User asks for image classification, age/gender/emotion detection, or
any HuggingFace `transformers` model.

**Export via optimum-cli (recommended):**

```python
import subprocess
subprocess.run([
    "optimum-cli", "export", "openvino",
    "--model", "dima806/fairface_age_image_detection",
    "fairface_age_image_detection",
    "--weight-format", "int8",
], check=True)
model_file = "fairface_age_image_detection/openvino_model.xml"
```

Source: `samples/gstreamer/python/face_detection_and_classification/face_detection_and_classification.py`

**Export via optimum-cli for ONNX → OpenVINO (two-step):**

```python
subprocess.run([
    "optimum-cli", "export", "onnx",
    "--model", "PekingU/rtdetr_v2_r50vd",
    "--task", "object-detection",
    "--opset", "18", "--width", "640", "--height", "640",
    "rtdetr_v2_r50vd",
], check=True)
subprocess.run(["ovc", "model.onnx"], check=True)
```

Source: `samples/gstreamer/python/smart_nvr/smart_nvr.py`

### 3. PaddlePaddle Models (OCR, detection, segmentation)

**When to use:** User asks for OCR (PaddleOCR), or any PaddlePaddle model from HuggingFace.

**CRITICAL:** PaddlePaddle v3+ models use PIR format (`.json` + `.pdiparams`), **not** the
older `.pdmodel` format. `ovc` cannot read PIR format directly. You must use a two-step
conversion: `paddle2onnx` → `ovc`.

**Export pattern — paddle2onnx → ovc (two-step):**

```python
import subprocess

# Step 1: Download entire model repo (contains inference.json + inference.pdiparams)
subprocess.run([
    sys.executable, "-c",
    f"from huggingface_hub import snapshot_download; "
    f"snapshot_download(repo_id='{model_id}', local_dir='{paddle_dir}')"
], check=True)

# Step 2: paddle2onnx — PaddlePaddle PIR → ONNX
subprocess.run([
    "paddle2onnx",
    "--model_dir", str(paddle_dir),
    "--model_filename", "inference.json",      # PIR format, NOT .pdmodel
    "--params_filename", "inference.pdiparams",
    "--save_file", str(onnx_file),
    "--opset_version", "14",
], check=True)

# Step 3: ovc — ONNX → OpenVINO IR
subprocess.run([
    "ovc", str(onnx_file), "--output_model", str(ov_model_xml)
], check=True)
```

**Character dictionary extraction (PaddleOCR):**

PaddleOCR models store their character dictionary inside `config.json`, not in separate
text files. Extract it with:

```python
import json
with open(paddle_dir / "config.json") as f:
    config = json.load(f)
char_dict = config["PostProcess"]["character_dict"]  # list of 18383 characters
with open(dict_path, "w") as f:
    f.write("\\n".join(char_dict) + "\\n")
```

**⚠ Common mistake:** Do NOT assume PaddlePaddle uses `.pdmodel` files. Check the repo
first with `list_repo_files()`. New PP-OCRv5 models use `inference.json` (PIR format).

**⚠ Common mistake:** Do NOT try `ovc inference.json` directly — it will fail with
"Cannot recognize input model". Always convert through ONNX first.

**Dynamic shapes:** PaddlePaddle models often export with dynamic batch and width dimensions.
Use `model.reshape()` to set static shapes before compilation. See Pattern 13 in
Design Patterns Reference.

Source: `samples/gstreamer/python/license_plate_recognition/license_plate_recognition.py`

**Requirements:**
```
paddlepaddle
paddle2onnx
```

### 4. Vision-Language Models (VLM) for gvagenai

**When to use:** User asks for VLM-based alerting, scene description, or image-text inference.

VLM models must be exported with the `image-text-to-text` task:

```python
import subprocess
subprocess.run([
    "optimum-cli", "export", "openvino",
    "--model", model_id,                 # e.g. "OpenGVLab/InternVL3_5-2B"
    "--task", "image-text-to-text",
    "--trust-remote-code",
    str(output_dir),
], check=True)
```

Source: `samples/gstreamer/python/vlm_alerts/vlm_alerts.py`

Recommended small models for edge: `OpenGVLab/InternVL3_5-2B`, `openbmb/MiniCPM-V-4_5`,
`Qwen/Qwen2.5-VL-3B-Instruct`, `HuggingFaceTB/SmolVLM2-2.2B-Instruct`.

## IMPORTANT: Verify HuggingFace Model Contents Before Writing Code

**Always check the actual files in a HuggingFace repository before writing download code.**
Do NOT assume filenames — they vary across repos, even for the same model architecture.

```python
from huggingface_hub import list_repo_files
files = list_repo_files("owner/model-name")
for f in files:
    print(f)
```

Common filename assumptions that fail:
- `best.pt` → may actually be `license-plate-finetune-v1s.pt` or `model.pt`
- `model.pdmodel` → may actually be `inference.json` (PaddlePaddle PIR format)
- `openvino_model.xml` → may need export first, only `.pt` or `.onnx` files available

## Caching Convention

All samples use a **check-then-download** pattern to avoid redundant exports:

```python
model_path = MODELS_DIR / f"{model_id}_int8_openvino_model" / f"{model_id}.xml"
if not model_path.exists():
    # ... export ...
return model_path.resolve()
```

Always cache exported models under a `models/` subdirectory relative to the sample app.

## Model Parameters for DLStreamer Elements

| Element | Property | Value | Notes |
|---------|----------|-------|-------|
| `gvadetect` | `model` | path to `.xml` | Object detection model |
| `gvadetect` | `device` | `GPU`, `CPU`, `NPU` | Inference device |
| `gvadetect` | `batch-size` | 1–8 | Frames per inference batch |
| `gvadetect` | `threshold` | 0.0–1.0 | Confidence threshold |
| `gvadetect` | `pre-process-backend` | `va`, `opencv` | `va` for GPU memory, `opencv` for CPU |
| `gvaclassify` | `model` | path to `.xml` | Classification model |
| `gvaclassify` | `model-proc` | path to `.json` | Optional model-proc for label mapping |
| `gvagenai` | `model-path` | path to model dir | VLM model directory |
| `gvagenai` | `prompt` | text string | Inline prompt |
| `gvagenai` | `prompt-path` | path to `.txt` | Prompt from file (use for long prompts) |
| `gvagenai` | `generation-config` | key=value pairs | e.g. `"max_new_tokens=50,num_beams=4"` |
| `gvagenai` | `frame-rate` | float | Frames per second to process |
| `gvagenai` | `chunk-size` | int | Number of frames per inference chunk |

## Requirements

Typical `requirements.txt` entries by model source:

```
# Ultralytics YOLO
ultralytics>=8.4
--extra-index-url https://download.pytorch.org/whl/cpu

# HuggingFace transformers + OpenVINO export
optimum[openvino]
huggingface_hub

# PaddlePaddle models (OCR, etc.)
paddlepaddle
paddle2onnx
openvino  # for ovc model converter

# Custom elements with pixel access
numpy
opencv-python  # or opencv-python-headless

# Common
PyGObject>=3.50.0
```
