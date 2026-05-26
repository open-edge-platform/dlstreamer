# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Download and export AI models for <APPLICATION_NAME>.

Run this script once before starting the pipeline application:
    python3 export_models.py
"""

import argparse
import json
import shutil
import subprocess  # nosec B404
import sys
from pathlib import Path

from huggingface_hub import hf_hub_download, snapshot_download

MODELS_DIR = Path(__file__).resolve().parent / "models"


# ── Model Export Functions ────────────────────────────────────────────────────
# Each function:
#   1. Checks if the exported .xml already exists (idempotent / cached)
#   2. Downloads the source model from HuggingFace
#   3. Exports to OpenVINO IR format
#   4. Returns the path to the .xml file


def export_yolo_detection(repo_id: str, pt_filename: str, revision: str = "main") -> Path:
    """Download a YOLO .pt from HuggingFace and export to OpenVINO IR INT8.

    Uses Ultralytics YOLO export with INT8 quantization for best performance.

    Note: Ultralytics export() returns a directory whose name varies by quantization
    settings (e.g. <stem>_int8_openvino_model/ for int8=True). This function
    normalizes it to a predictable <stem>_openvino/ name.
    """
    model_stem = pt_filename.replace(".pt", "")
    ov_dir = MODELS_DIR / f"{model_stem}_openvino"

    xml_files = list(ov_dir.glob("*.xml")) if ov_dir.exists() else []
    if xml_files:
        print(f"[YOLO] Model already exists: {xml_files[0]}")
        return xml_files[0]

    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    print(f"[YOLO] Downloading {repo_id} / {pt_filename}...")
    pt_path = hf_hub_download(
        repo_id=repo_id, 
        filename=pt_filename, 
        local_dir=str(MODELS_DIR),
        revision=revision
    )

    print("[YOLO] Exporting to OpenVINO IR (INT8)...")
    from ultralytics import YOLO

    model = YOLO(str(pt_path))
    exported_path = model.export(format="openvino", dynamic=True, int8=True)

    # Rename export dir to predictable name
    export_dir = Path(exported_path)
    if export_dir != ov_dir:
        if ov_dir.exists():
            shutil.rmtree(ov_dir)
        export_dir.rename(ov_dir)

    xml_files = list(ov_dir.glob("*.xml"))
    if not xml_files:
        raise FileNotFoundError(f"No .xml found in {ov_dir}")
    print(f"[YOLO] Model ready: {xml_files[0]}")
    return xml_files[0]


def export_paddleocr(model_id: str, revision: str = "main") -> Path:
    """Download PaddleOCR model and convert PIR → ONNX → OpenVINO IR FP16.

    PaddlePaddle v3+ uses PIR format (.json + .pdiparams), not .pdmodel.
    Conversion is two-step: paddle2onnx then ovc.
    """
    model_name = model_id.split("/")[-1]
    ocr_dir = MODELS_DIR / model_name
    fp16_dir = ocr_dir / "FP16"
    ov_model = fp16_dir / f"{model_name}.xml"

    if ov_model.exists():
        print(f"[OCR] Model already exists: {ov_model}")
        return ov_model

    ocr_dir.mkdir(parents=True, exist_ok=True)
    paddle_dir = ocr_dir / "paddle_model"

    # Step 1: Download from HuggingFace
    print(f"[OCR] Downloading {model_id} from HuggingFace...")
    snapshot_download(
        repo_id=model_id, 
        local_dir=str(paddle_dir),
        revision=revision
    )

    # Step 2: PaddlePaddle PIR → ONNX
    onnx_file = ocr_dir / "model.onnx"
    print("[OCR] Converting PaddlePaddle PIR → ONNX...")
    
    # Use absolute path for paddle2onnx to avoid partial path issues
    paddle2onnx_cmd = shutil.which("paddle2onnx")
    if not paddle2onnx_cmd:
        raise RuntimeError("paddle2onnx not found in PATH")
    
    subprocess.run(  # nosec B603, B607
        [
            paddle2onnx_cmd,  # Use full path
            "--model_dir", str(paddle_dir),
            "--model_filename", "inference.json",       # PIR format, NOT .pdmodel
            "--params_filename", "inference.pdiparams",
            "--save_file", str(onnx_file),
            "--opset_version", "14",
        ],
        check=True,
    )

    # Step 3: ONNX → OpenVINO IR FP16
    fp16_dir.mkdir(parents=True, exist_ok=True)
    print("[OCR] Converting ONNX → OpenVINO IR (FP16)...")
    
    # Use absolute path for ovc to avoid partial path issues
    ovc_cmd = shutil.which("ovc")
    if not ovc_cmd:
        raise RuntimeError("ovc not found in PATH")
    
    subprocess.run(  # nosec B603, B607
        [ovc_cmd, str(onnx_file), "--output_model", str(ov_model), "--compress_to_fp16"],
        check=True,
    )

    # Step 4: Extract character dictionary from config.json (PaddleOCR-specific)
    config_src = paddle_dir / "config.json"
    if config_src.exists():
        shutil.copy2(str(config_src), str(fp16_dir / "config.json"))
        with open(config_src) as f:
            config = json.load(f)
        char_dict = config.get("PostProcess", {}).get("character_dict", [])
        if char_dict:
            dict_path = fp16_dir / "character_dict.txt"
            with open(dict_path, "w") as f:
                f.write("\n".join(char_dict) + "\n")
            print(f"[OCR] Character dictionary extracted ({len(char_dict)} chars)")

    # Cleanup intermediate files
    onnx_file.unlink(missing_ok=True)
    shutil.rmtree(str(paddle_dir), ignore_errors=True)

    print(f"[OCR] Model ready: {ov_model}")
    return ov_model


def export_hf_transformer(model_id: str, weight_format: str = "int8", revision: str = "main") -> Path:
    """Export a HuggingFace transformer model via optimum-cli."""
    model_name = model_id.split("/")[-1]
    output_dir = MODELS_DIR / model_name
    model_xml = output_dir / "openvino_model.xml"

    if model_xml.exists():
        print(f"[HF] Model already exists: {model_xml}")
        return model_xml

    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    print(f"[HF] Exporting {model_id} via optimum-cli ({weight_format})...")
    
    # Use absolute path for optimum-cli to avoid partial path issues
    optimum_cmd = shutil.which("optimum-cli")
    if not optimum_cmd:
        raise RuntimeError("optimum-cli not found in PATH")
    
    subprocess.run(  # nosec B603, B607
        [
            optimum_cmd, "export", "openvino",
            "--model", model_id,
            "--weight-format", weight_format,
            "--revision", revision,
            str(output_dir),
        ],
        check=True,
    )

    print(f"[HF] Model ready: {model_xml}")
    return model_xml


# ── CLI ───────────────────────────────────────────────────────────────────────


def parse_args():
    p = argparse.ArgumentParser(description="Download models for <APPLICATION_NAME>")
    # Add model-specific arguments here, e.g.:
    # p.add_argument("--variant", default="s", choices=["n","s","m","l","x"])
    return p.parse_args()


def main():
    args = parse_args()

    # Call the appropriate export functions for your models:
    # det = export_yolo_detection("repo/name", "model.pt", revision="v1.0")
    # ocr = export_paddleocr("PaddlePaddle/PP-OCRv5_server_rec", revision="main")
    # cls = export_hf_transformer("org/model-name", weight_format="int8", revision="main")

    print("\n=== All models ready ===")


if __name__ == "__main__":
    main()
