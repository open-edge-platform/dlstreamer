# Model Preparation Reference

DLStreamer inference elements (`gvadetect`, `gvaclassify`, `gvagenai`) consume models in
**OpenVINO IR format** (`.xml` + `.bin`). Source models come from multiple ecosystems; each has
a different download-and-export path. In addition, DLStreamer reads pre- and post-processing
information from the ecosystem model metadata files (Ultralytics, HuggingFace and PaddlePaddle).


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

**Export pattern — subprocess (when DLStreamer is already loaded):**

Ultralytics export creates a new OpenVINO runtime instance that can clash with DLStreamer's
runtime. The **recommended approach** is to use a separate `download_models.py` script
(see Design Patterns → Pattern 11) that users run once before starting the pipeline app.
Alternatively, call the export from a subprocess:

```python
import subprocess, sys
result = subprocess.run(
    [sys.executable, "download_models.py"],
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

If an Ultralytics model is located on the HuggingFace hub, download it first to the local disk and
then use the Ultralytics model exporter as described in section #1.

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

**When to use:** User asks for image classification, age/gender/emotion detection, or
any HuggingFace `transformers` model.

**Export via optimum-cli (recommended):**

The `optimum-cli` tool from the `optimum-intel` package is the recommended way to export
HuggingFace models to OpenVINO IR format:

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

**When to use:** User asks for VLM-based alerting, scene description, or image-text inference.

VLM models must be exported with the `image-text-to-text` task:

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

Model-proc (model processing) JSON files are deprecated; please do not use them with inference models. 

## Weight Compression Guidance

| Compression | Flag | Best For | Quality Impact |
|-------------|------|----------|----------------|
| FP32 | (default) | Maximum accuracy | None |
| FP16 | `half=True` (Ultralytics), `--compress_to_fp16` (ovc) | GPU/NPU inference, reduced size | Negligible |
| INT8 | `int8=True` (Ultralytics) | GPU/NPU inference, reduced size | Negligible |
| INT8 | `--weight-format int8` (optimum-cli) | HuggingFace transformer models | Minor |
| INT4 | `--weight-format int4` (optimum-cli) | Large LLM/VLM models | Moderate, acceptable for VLMs |

> **Note:** Ultralytics INT8 export (`int8=True`) requires the `nncf` package. Pin `nncf`
> to the version discovered via `pip show nncf | grep Version` on the host (see
> [Version Discovery Procedure](#version-discovery-procedure) below).
> INT8 export triggers NNCF calibration which may take some time and may appear to hang. For iterative development, use `half=True` (FP16) first; switch to `int8=True` for production builds.

> **Recommendation:** Use **INT8** (`int8=True`) for Ultralytics YOLO models.
> Use INT8 for HuggingFace transformer classification models. Use INT4 for VLM models.

## Requirements

Prefer using `==` pins (e.g. `ultralytics==8.4.33`) in `export_requirements.txt`, over open ranges like `>=8.3.0`.
Open ranges pull untested releases that may change export behavior or break backward compatibility.

### Version Discovery Procedure

Sample apps in this repo may pin **older** package versions. Do **not** blindly copy them.
Instead, discover the latest version for each package using this priority order:

1. **OpenVINO** — match the OpenVINO runtime bundled with DLStreamer.

   **Host install** (OpenVINO is installed separately under `/opt/intel/openvino_*`):
   ```bash
   cat /opt/intel/openvino_*/runtime/version.txt
   # Example output: 2026.0.0-20965-c6d6a13a886-releases/2026/0
   # Pin the release part: openvino==2026.0.0
   ```

   **Docker image** (OpenVINO is installed via deb packages — there is no
   `/opt/intel/openvino_*` directory; use the Python query instead):
   ```bash
   docker run --rm <dlstreamer_image> \
       python3 -c "import openvino; print(openvino.__version__)"
   ```

   **Fallback** (works in both environments when OpenVINO Python is importable):
   ```bash
   python3 -c "import openvino; print(openvino.__version__)"
   ```

2. **NNCF** — should be compatible with OpenVINO runtime version.
   Please check this page to find version match between OpenVINO and NNCF versions:
   https://github.com/openvinotoolkit/nncf/blob/develop/docs/Installation.md

3. **Ultralytics** — has its own dependencies on OpenVINO and NNCF that must match
   the versions from steps 1 and 2. OpenVINO is declared in pip `[export]` extras
   (e.g. `openvino>=2024.0.0`); NNCF is checked **at runtime** inside `exporter.py`
   (e.g. `nncf>=2.14.0`). To find the latest compatible Ultralytics version:
   ```bash
   # Get latest version
   pip index versions ultralytics 2>/dev/null | head -1

   # Download its wheel and inspect constraints
   pip download ultralytics==<VER> --no-deps -d /tmp/ul_check
   # Check openvino constraint (pip metadata):
   unzip -p /tmp/ul_check/ultralytics-*.whl \
       '*/METADATA' | grep -i openvino
   # Check nncf constraint (runtime, in exporter.py):
   unzip -p /tmp/ul_check/ultralytics-*.whl \
       ultralytics/engine/exporter.py | grep 'check_requirements.*nncf'
   ```
   Verify that the OpenVINO version from step 1 satisfies the `openvino>=X` constraint,
   and the NNCF version from step 2 satisfies the `nncf>=Y` constraint. If the latest
   Ultralytics is incompatible, step back to the previous minor version and re-check.

4. **optimum-intel** `optimum[openvino]` must be compatible with OpenVINO runtime version.
    Inspect: https://raw.githubusercontent.com/openvinotoolkit/openvino.genai/refs/heads/releases/<OV major>/<OV minor>/samples/export-requirements.txt

> **Rule:** Always run the version discovery commands **before** writing `export_requirements.txt`.
> Use the discovered versions as exact `==` pins.

> **CRITICAL — CPU-only PyTorch:** Always add `--extra-index-url https://download.pytorch.org/whl/cpu` as the
> **first line** of `export_requirements.txt` (before any package that depends on PyTorch).
> Without this, pip resolves the default CUDA-enabled PyTorch which downloads unnecessary dependencies and
> takes a very long time to install. Model export only needs CPU inference.

Typical `requirements.txt` entries by model source:

```
# IMPORTANT: CPU-only PyTorch — must appear before any torch-dependent package
--extra-index-url https://download.pytorch.org/whl/cpu

# OpenVINO Python version (pin to match DLStreamer runtime — query with: python3 -c "import openvino; print(openvino.__version__)")
openvino==2026.0.0
nncf==3.0.0  # required for int8=True quantization (query with: pip show nncf | grep Version)

# Ultralytics YOLO (query with: pip show ultralytics | grep Version)
ultralytics==8.4.33

# HuggingFace transformers + OpenVINO export
optimum[openvino]
huggingface_hub

# PaddlePaddle models (OCR, etc.) — paddle2onnx → ovc conversion
paddlepaddle  # CPU-only variant (paddlepaddle-gpu is the GPU package)
paddle2onnx
onnx               # required by paddle2onnx for ONNX graph construction
onnxruntime         # required by paddle2onnx for model validation
onnx_graphsurgeon   # required by paddle2onnx for ONNX graph optimization

# Custom elements with pixel access
numpy
opencv-python  # or opencv-python-headless

# Common
PyGObject==3.50.0  # 3.50.2 depends on girepository-2.0 and breaks backward-compatibility
```
