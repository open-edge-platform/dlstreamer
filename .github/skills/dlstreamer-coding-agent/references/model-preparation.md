# Model Preparation Reference

DL Streamer inference elements consume models in **OpenVINO IR format** (`.xml` + `.bin`).
Pre/post-processing info is read from ecosystem metadata (Ultralytics, HuggingFace, PaddlePaddle).


## Model Sources and Export Methods

### 1. Ultralytics YOLO Models (detection / segmentation)

**When to use:** User asks for object detection, segmentation, or open-vocabulary detection
with YOLO, YOLOv8, YOLO11, YOLOE, or YOLO26.

**Export pattern — in-process (simple apps):**

> **IMPORTANT — keep all files in `models/`:** Ultralytics `YOLO("name.pt")` downloads
> weights to the **current working directory**. Always pass an explicit path under
> `MODELS_DIR` so intermediate `.pt` files, exported OpenVINO directories, and any
> calibration artifacts all live inside `models/` — not in the application root.

```python
from pathlib import Path
from ultralytics import YOLO

MODELS_DIR = Path(__file__).resolve().parent / "models"
MODELS_DIR.mkdir(parents=True, exist_ok=True)

pt_path = MODELS_DIR / "yolo11n.pt"
model = YOLO(str(pt_path))                                      # download weights into models/
path  = model.export(format="openvino", dynamic=True, int8=True) # export to OV IR
model_file = f"{path}/yolo11n.xml"
```

Source: `samples/gstreamer/python/face_detection_and_classification/face_detection_and_classification.py`

**Export pattern — subprocess (when DL Streamer is already loaded):**

Ultralytics export can clash with DL Streamer's OpenVINO runtime. Use a separate
`export_models.py` script (see [Separate Export Script](#separate-export-script))
or call from a subprocess:

```python
import subprocess, sys
result = subprocess.run(
    [sys.executable, "export_models.py"],
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

### 2. HuggingFace Ultralytics Models

If an Ultralytics model is located on the HuggingFace Hub, download it first to the local disk and
then use the Ultralytics model exporter as described in Section 1.

> **IMPORTANT:** Do not assume `.pt` file names (e.g. `best.pt`, `model.pt`). HuggingFace repos
> use varied naming conventions. Always check the actual files in the repo's "Files" tab on
> huggingface.co before writing the download script.

```python
from huggingface_hub import hf_hub_download

model_path = hf_hub_download(
    repo_id="arnabdhar/YOLOv8-Face-Detection",
    filename="model.pt",                  # verify actual filename in HF repo!
    local_dir=local_models_dir,
    )
```

Source: `samples/gstreamer/python/face_detection_and_classification/face_detection_and_classification.py`

### 3. HuggingFace Transformer Models (classification / VLM)

**When to use:** Image classification, age/gender/emotion detection, or any HuggingFace `transformers` model.

**Export via optimum-cli (recommended):**

```bash
# Basic export
optimum-cli export openvino --model <model_id> <output_dir>

# With INT8 weight quantization
optimum-cli export openvino --model <model_id> --weight-format int8 <output_dir>

# With INT4 weight quantization (for large models / VLMs)
optimum-cli export openvino --model <model_id> --weight-format int4 <output_dir>
```

**Python subprocess pattern:**

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

**Export via optimum-cli for ONNX → OpenVINO (two-step, when direct export fails):**

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

**Common `optimum-cli` task values:**

| Task | Use Case |
|------|----------|
| `image-classification` | Image classification models |
| `object-detection` | Object detection models (DETR, RT-DETR) |
| `image-text-to-text` | Vision-Language Models (VLM) |
| `text-generation` | Language models |
| `automatic-speech-recognition` | Audio transcription (Whisper) |

### 4. Vision-Language Models (VLM) for gvagenai

**When to use:** VLM-based alerting, scene description, or image-text inference.

Export with `image-text-to-text` task:

```bash
optimum-cli export openvino \
    --model <model_id> \
    --task image-text-to-text \
    --trust-remote-code \
    --weight-format int4 \
    <output_dir>
```

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

### 5. PaddlePaddle OCR Models

**When to use:** OCR (PaddleOCR) or any PaddlePaddle model from HuggingFace.

**CRITICAL:** PaddlePaddle v3+ uses PIR format (`.json` + `.pdiparams`), not `.pdmodel`.
`ovc` cannot read PIR directly — use `paddle2onnx → ovc`.

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
    f.write("\n".join(char_dict) + "\n")
```


**Requirements:**
```
paddlepaddle  # CPU-only variant; use paddlepaddle-gpu if GPU conversion is needed
paddle2onnx
onnx               # required by paddle2onnx for ONNX graph construction
onnxruntime         # required by paddle2onnx for model validation
onnx_graphsurgeon   # required by paddle2onnx for ONNX graph optimization
```

### 6. Audio Models for gvaaudiodetect / gvaaudiotranscribe

**When to use:** User asks for audio event detection or audio transcription.

For audio transcription with `gvaaudiotranscribe`, Whisper models are used and should be
exported via `optimum-cli`:

```bash
optimum-cli export openvino \
    --model openai/whisper-base \
    --task automatic-speech-recognition \
    whisper-base-ov
```

### 7. OpenVINO Model Zoo / Open Model Zoo Models

OpenVINO Model Zoo and related models are deprecated. Please discourage users from accessing this repository.
Recommend a model from HuggingFace Hub instead.


## Model-Proc Files

Model-proc (model processing) JSON files are deprecated; do not use them with inference models.

## Weight Compression Guidance

| Compression | Flag | Best For | Quality Impact |
|-------------|------|----------|----------------|
| FP16 | `half=True` (Ultralytics), `--compress_to_fp16` (ovc) | GPU/NPU inference, reduced size | Negligible |
| INT8 | `int8=True` (Ultralytics) | GPU/NPU inference, reduced size | Negligible |
| INT8 | `--weight-format int8` (optimum-cli) | HuggingFace transformer models | Minor |
| INT4 | `--weight-format int4` (optimum-cli) | Large LLM/VLM models | Moderate, acceptable for VLMs |

> **Note:** Ultralytics INT8 export (`int8=True`) requires the `nncf` package. Pin `nncf`
> to the version discovered via `pip show nncf | grep Version` on the host (see
> [Version Discovery Procedure](#version-discovery-procedure) below).

## Requirements

Prefer using `==` pins (e.g. `ultralytics==8.4.33`) in `export_requirements.txt` over open ranges like `>=8.3.0`.
Open ranges pull untested releases that may change export behavior or break backward compatibility.

### Version Discovery Procedure

Sample apps in this repo may pin **older** package versions. Do **not** blindly copy them.
Instead, discover the latest version for each package using this priority order:

> **Quick discovery (preferred):**
> ```bash
> docker run --rm <dlstreamer_image> python3 -c "
> import openvino; print(f'openvino=={openvino.__version__.split(\"-\")[0]}')
> "
> ```
> Then check NNCF compatibility at https://github.com/openvinotoolkit/nncf/blob/develop/docs/Installation.md

If quick discovery fails, discover manually:

1. **OpenVINO** — `python3 -c "import openvino; print(openvino.__version__)"` or
   `cat /opt/intel/openvino_*/runtime/version.txt` (host install only).

2. **NNCF** — match to OpenVINO version per
   https://github.com/openvinotoolkit/nncf/blob/develop/docs/Installation.md

3. **Ultralytics** — verify its OpenVINO/NNCF constraints match steps 1-2:
   ```bash
   pip download ultralytics==<VER> --no-deps -d /tmp/ul_check
   unzip -p /tmp/ul_check/ultralytics-*.whl '*/METADATA' | grep -i openvino
   unzip -p /tmp/ul_check/ultralytics-*.whl ultralytics/engine/exporter.py | grep 'check_requirements.*nncf'
   ```

4. **optimum-intel** — must match OpenVINO version. Check:
   `https://raw.githubusercontent.com/openvinotoolkit/openvino.genai/refs/heads/releases/<OV major>/<OV minor>/samples/export-requirements.txt`


Typical `requirements.txt` entries by model source:

```
# CPU-only PyTorch — must appear before torch-dependent packages
--extra-index-url https://download.pytorch.org/whl/cpu

# OpenVINO Python version (pin to match DL Streamer runtime — query with: python3 -c "import openvino; print(openvino.__version__)")
openvino==2026.0.0
nncf==3.0.0  # required for int8=True quantization (query with: pip show nncf | grep Version)

# Ultralytics YOLO (query with: pip show ultralytics | grep Version)
ultralytics==8.4.33

# HuggingFace transformers + OpenVINO export
optimum[openvino]
huggingface_hub
onnx               # required by optimum-cli for ONNX export and dynamic shape fixing
onnxruntime         # required by optimum-cli for ONNX model validation

# PaddlePaddle models (OCR, etc.) — paddle2onnx → ovc conversion
paddlepaddle  # CPU-only variant (paddlepaddle-gpu is the GPU package)
paddle2onnx
onnx               # (already listed above — shared with optimum-cli)
onnxruntime         # (already listed above — shared with optimum-cli)
onnx_graphsurgeon   # required by paddle2onnx for ONNX graph optimization

# Custom elements with pixel access
numpy
opencv-python
PyGObject==3.50.0  # 3.50.2 breaks backward-compatibility
```

---

## Separate Export Script

Use a separate `export_models.py` script to download and export AI models. See the
[Export Models Template](../assets/export-models-template.py). Each function should:

1. Check if `.xml` already exists (idempotent)
2. Download the source model
3. Export to OpenVINO IR
4. Return the `.xml` path
