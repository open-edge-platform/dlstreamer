# Model Conversion Scripts

This folder contains standalone conversion CLIs:

- `download_hf_models.py` — Convert Hugging Face models to OpenVINO.
- `download_ultralytics_models.py` — Convert Ultralytics YOLO models to OpenVINO.


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

## Output notes

- Hugging Face exports are written under `<outdir>/<model_name>/`.
- Ultralytics export output is moved into the specified `--outdir`.
