# Model Conversion Scripts

This folder contains standalone conversion CLIs:

- `download_hf_models.py` — Convert Hugging Face models to OpenVINO.
- `download_ultralytics_models.py` — Convert Ultralytics YOLO models to OpenVINO.
- `download_timm_models.py` — Convert supported TIMM image-classification models to OpenVINO.


## Prerequisites

1. Create and activate a virtual environment:
```code
   python3 -m venv .model_download_venv
   source .model_download_venv/bin/activate
   ```

2. Install dependencies:
```code
   curl -LO https://raw.githubusercontent.com/openvinotoolkit/openvino.genai/refs/heads/releases/2026/0/samples/export-requirements.txt
   pip install -r export-requirements.txt -r requirements.txt
   ```

The local `requirements.txt` installs model-source specific packages used by
these scripts.

## 1) Hugging Face conversion

Script: `download_hf_models.py`

### Command

```bash
python download_hf_models.py \
  --model <huggingface_model_id> \
  [--outdir <output_dir>] \
  [--token <hf_token>] \
  [--extra_args <arg1> <arg2> ...]
```

### Arguments

- `--model` (required): Hugging Face model id (for example: `google/gemma-3-4b-it`).
- `--outdir` (optional, default `.`): Output directory.
- `--token` (optional): HF token for gated/private models.
- `--extra_args` (optional): Extra arguments forwarded to `optimum-cli export openvino` for standard exports. Values that start with `--` are supported.

### Behavior

The script classifies a model into one of three support levels:

- `0` — Standard export path: calls `optimum-cli export openvino`.
- `1` — Custom export path: handled in `hf_utils.py` (currently CLIP/RT-DETR custom converters).
- `2` — Unsupported: prints an error and exits with code `1`.

### Examples

```bash
# Standard HF export
python download_hf_models.py --model google/gemma-3-4b-it --outdir ./exports

# Pass extra args through to optimum-cli
python download_hf_models.py --model openbmb/MiniCPM-V-2_6 --extra_args --weight-format int4 --outdir ./exports

# Private/gated model
python download_hf_models.py --model <org/private-model> --token <HF_TOKEN> --outdir ./exports
```

## 2) Ultralytics conversion

Script: `download_ultralytics_models.py`

### Command

```bash
python download_ultralytics_models.py \
  --model <ultralytics_name_or_pt_path> \
  [--outdir <output_dir>] \
  [--half] \
  [--int8]
```

### Arguments

- `--model` (required): Ultralytics model name or local `.pt` path.
- `--outdir` (optional, default `.`): Output directory.
- `--half` (optional): Export in FP16.
- `--int8` (optional): Export in INT8.

### Examples

```bash
# Export by model name
python download_ultralytics_models.py --model yolo11n.pt --outdir ./exports

# Export a local checkpoint in FP16
python download_ultralytics_models.py --model /path/to/model.pt --outdir ./exports --half

# Export in INT8
python download_ultralytics_models.py --model yolo11s.pt --outdir ./exports --int8
```

## 3) TIMM conversion

Script: `download_timm_models.py`

This script exports selected PyTorch Image Models (TIMM) image-classification models to OpenVINO IR.
Run `list-models` in your model-download environment to print the supported
model names.

### Commands

```bash
python download_timm_models.py list-models

python download_timm_models.py import \
  --model <timm_model_name> \
  [--precision fp16|int8|both] \
  [--output-dir <models_path>] \
  [--calibration-data <image_dir>] \
  [--calibration-subset-size <num_images>]
```

### Arguments

- `list-models`: Lists supported TIMM model names.
- `--model` (required for `import`): TIMM model name from `list-models`.
- `--precision` (optional, default `fp16`): Supports `fp16`, `int8`, or `both`.
- `--output-dir` (optional): Output root. Defaults to `MODELS_PATH` when set.
- `--calibration-data` (required for `int8` or `both`): Directory with representative calibration images.
- `--calibration-subset-size` (optional, default `300`): Maximum number of calibration images to use for INT8.

Existing TIMM exports in the target folder are replaced only after the new
OpenVINO IR has been saved and verified.

### Precisions

TIMM export supports FP16 and INT8. FP16 uses OpenVINO FP16 weight compression.
INT8 uses NNCF post-training quantization and requires representative image
calibration data.

### Examples

```bash
# list supported TIMM models
python download_timm_models.py list-models

# export in FP16
python download_timm_models.py import \
  --model mobilenetv3_small_100 \
  --precision fp16 \
  --output-dir "${MODELS_PATH}"

# export INT8 only (use representative images for INT8 calibration)
python download_timm_models.py import \
  --model mobilenetv3_small_100 \
  --precision int8 \
  --output-dir "${MODELS_PATH}" \
  --calibration-data "${CALIBRATION_IMAGES_DIR}"

# export FP16 and INT8 (use representative images for INT8 calibration)
python download_timm_models.py import \
  --model mobilenetv3_small_100 \
  --precision both \
  --output-dir "${MODELS_PATH}" \
  --calibration-data "${CALIBRATION_IMAGES_DIR}"
```

## Output notes

- Hugging Face exports are written under `<outdir>/<model_name>/`.
- Ultralytics export output is moved into the specified `--outdir`.
- TIMM exports are written under `<output-dir>/public/<model_name>/FP16/<model_name>.xml`
  and/or `<output-dir>/public/<model_name>/INT8/<model_name>.xml` with matching
  `.bin` files.
