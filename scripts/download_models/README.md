# Model Conversion Scripts

This folder contains standalone conversion CLIs:

- `download_hf_models.py` — Convert Hugging Face models to OpenVINO.
- `download_ultralytics_models.py` — Convert Ultralytics YOLO models to OpenVINO.
- `download_timm_models.py` — Convert supported TIMM (PyTorch Image Models) image-classification models to OpenVINO.
- `download_other_models.sh` — Download and convert selected non-HF/non-Ultralytics helper models.

Model list files used by automation are stored in:

- `scripts/download_models/model_lists/hf_models.txt`
- `scripts/download_models/model_lists/ultralytics_models.txt`
- `scripts/download_models/model_lists/timm_models.txt`

## Model Reference Format (`@...`)

The `@...` suffix means "pin this exact version", but the source differs by tool:

- Hugging Face (`download_hf_models.py` and `download_timm_models.py`):
  - Format: `repo_id@revision`
  - Example: `openai/clip-vit-base-patch32@3d74acf9a28c67741b2f4f2ea7635f0aaf6f0268`
  - `revision` is a Hugging Face repo revision (typically a commit SHA).

- Ultralytics (`download_ultralytics_models.py`):
  - Format: `model.pt@tag`
  - Example: `yolo11n.pt@v8.4.0`
  - `tag` is a GitHub release tag from `ultralytics/assets`.

If `@...` is omitted:

- `download_hf_models.py` uses the current latest Hugging Face revision at runtime.
- `download_timm_models.py` uses the current latest Hugging Face revision at runtime.
- `download_ultralytics_models.py` resolves a model by local file first, then by Ultralytics latest weights.
- `download_other_models.sh` uses fixed script-defined sources (no per-model `@...` pin syntax).


## 1) Hugging Face conversion

Script: `download_hf_models.py`

Dependencies file: `requirements_download_hf_models.txt`

### Setup

```bash
python3 -m venv .hf_models_venv
source .hf_models_venv/bin/activate  # On Windows: .hf_models_venv\Scripts\activate
pip install -r requirements_download_hf_models.txt
```

### Command

```bash
python download_hf_models.py \
  --model <huggingface_model_id> \
  [--outdir <output_dir>] \
  [--token <hf_token>] \
  [--extra_args <arg1> <arg2> ...]
```

### Arguments

- `--model` (required): Hugging Face model id. You can pass either a plain repo id such as `google/gemma-3-4b-it` or an explicit `repo_id@revision` override.
- `--outdir` (optional, default `.`): Output directory.
- `--token` (optional): HF token for gated/private models.
- `--extra_args` (optional): Extra arguments forwarded to `optimum-cli export openvino` for standard exports. Values that start with `--` are supported.

### Behavior

The script classifies a model into one of three support levels:

- `0` — Standard export path: calls `optimum-cli export openvino`.
- `1` — Custom export path: handled in `hf_utils.py` (currently CLIP/RT-DETR custom converters).
- `2` — Unsupported: prints an error and exits with code `1`.

When `--model` does not include `@revision`, the script downloads the latest available Hugging Face revision at runtime.
Use `repo_id@revision` to make runs reproducible.

### Examples

```bash
# Standard HF export
python download_hf_models.py --model google/gemma-3-4b-it --outdir ./exports

# Pass extra args through to optimum-cli
python download_hf_models.py --model openbmb/MiniCPM-V-2_6 --extra_args --weight-format int4 --outdir ./exports

# Private/gated model
python download_hf_models.py --model <org/private-model> --token <HF_TOKEN> --outdir ./exports

# Explicit revision override
python download_hf_models.py --model openai/clip-vit-base-patch32@3d74acf9a28c67741b2f4f2ea7635f0aaf6f0268 --outdir ./exports
```

## 2) Ultralytics conversion

Script: `download_ultralytics_models.py`

Dependencies file: `requirements_download_ultralytics_models.txt`

### Setup

```bash
python3 -m venv .ultralytics_models_venv
source .ultralytics_models_venv/bin/activate  # On Windows: .ultralytics_models_venv\Scripts\activate
pip install -r requirements_download_ultralytics_models.txt
```

### Command

```bash
python download_ultralytics_models.py \
  --model <ultralytics_name_or_pt_path> \
  [--outdir <output_dir>] \
  [--half] \
  [--int8]
```

### Arguments

- `--model` (required): Ultralytics model reference. Supported forms:
  - `<model>.pt` or `<model>` for latest weights resolved by Ultralytics.
  - `<model>@<revision>` to pin weights from `ultralytics/assets` GitHub release tag.
  - Hugging Face repo ID (e.g., `user/repo`).
  - Local `.pt` path.
- `--outdir` (optional, default `.`): Output directory. Callers can point it
  to a model and precision directory, for example
  `${MODELS_PATH}/public/yolo11n/FP16`.
- `--half` (optional): Export in FP16.
- `--int8` (optional): Export in INT8.

For Hugging Face repositories, the exported XML and BIN use the repository ID
with `/` replaced by `_` instead of the generic `model.xml` and `model.bin`.

DL Streamer auto-conversion supports Ultralytics detection, segmentation, pose, OBB, and classification exports.

### Examples

```bash
# Export by model name
python download_ultralytics_models.py --model yolo11n.pt --outdir ./exports

# Export pinned model from a specific ultralytics/assets release tag
python download_ultralytics_models.py --model yolo11n.pt@v8.3.0 --outdir ./exports --half

# Export a local checkpoint in FP16
python download_ultralytics_models.py --model /path/to/model.pt --outdir ./exports --half

# Export in INT8
python download_ultralytics_models.py --model yolo11s.pt --outdir ./exports --int8
```

## 3) TIMM conversion

Script: `download_timm_models.py`

Dependencies file: `requirements_download_timm_models.txt`

### Setup

```bash
python3 -m venv .timm_models_venv
source .timm_models_venv/bin/activate  # On Windows: .timm_models_venv\Scripts\activate
pip install -r requirements_download_timm_models.txt
```

This script exports a relevant set of Hugging Face-hosted PyTorch Image Models
(TIMM) image-classification models to OpenVINO IR using `optimum-cli`. Run
`list-models` in your model-download environment to print the supported model
names.

### Commands

```bash
python download_timm_models.py list-models

python download_timm_models.py import \
  --model <timm_model_name_or_timm_model_name@revision> \
  [--precision fp16|int8|both] \
  [--output-dir <models_path>]
```

### Arguments

- `list-models`: Lists supported TIMM model names.
- `--model` (required for `import`): TIMM model reference from `list-models`.
  Supports both `<timm_model_name>` (latest) and `<timm_model_name>@<huggingface_revision_sha>` (pinned).
- `--precision` (optional, default `fp16`): Supports `fp16`, `int8`, or `both`.
  INT8 uses Optimum weight-format quantization.
- `--output-dir` (optional): Output root. Defaults to `MODELS_PATH` when set.

Existing TIMM exports in the target folder are replaced only after the exported
OpenVINO IR has been read and re-saved successfully.
When `@revision` is provided, both export and `config.json` are resolved from that pinned revision.
Without `@revision`, the helper exports from the latest available Hugging Face revision at runtime.
Use `@revision` for reproducible runs.

### Precisions

TIMM export supports FP16 and INT8 through Optimum `--weight-format`. INT8 is
weight-format quantization, not full activation calibration.

### Examples

```bash
# list supported TIMM models
python download_timm_models.py list-models

# export in FP16
python download_timm_models.py import \
  --model mobilenetv3_small_100 \
  --precision fp16 \
  --output-dir "${MODELS_PATH}"

# export INT8 only
python download_timm_models.py import \
  --model mobilenetv3_small_100 \
  --precision int8 \
  --output-dir "${MODELS_PATH}"

# export FP16 and INT8
python download_timm_models.py import \
  --model mobilenetv3_small_100 \
  --precision both \
  --output-dir "${MODELS_PATH}"
```

## 4) Other Public Models (Shell)

Script: `download_other_models.sh`

This script downloads/converts a fixed set of helper models (for example, `centerface`, `hsemotion`, `deeplabv3`, `mars-small128`) into the `MODELS_PATH/public/...` layout.

### Setup

Set `MODELS_PATH` before running the script. The script creates and manages its
own virtual environments under `${HOME}/.virtualenvs/` and installs its Python
dependencies automatically.

### Command

```bash
./download_other_models.sh [MODEL] [QUANTIZE]
```

### Versioning Behavior

- This script does not accept per-model `@revision` or `@tag` syntax.
- Model versions are controlled by URLs and tool versions hardcoded in the script.
- To pin exact artifacts, keep the script revision pinned in git.

### Examples

```bash
# Download all supported "other" models
./download_other_models.sh all

# Download only mars-small128
./download_other_models.sh mars-small128
```

## Output notes

- Hugging Face exports are written under `<outdir>/<model_name>/`. For standard
  single-IR exports the helper normalizes them into `<outdir>/<model_name>/<precision>/`
  with matching `.xml` and `.bin` files; for multi-IR exports it preserves the
  original export layout under `<outdir>/<model_name>/`.
- Ultralytics export output is moved into the specified `--outdir`, which should
  normally already be the desired precision directory.
- TIMM exports are written under `<output-dir>/public/<model_name>/<precision>/`
  (e.g. `FP16/`, `INT8/`) with matching `.xml`, `.bin`, and `data_config.json` files.
- `download_other_models.sh` writes models under `${MODELS_PATH}/public/<model_name>/...`.
