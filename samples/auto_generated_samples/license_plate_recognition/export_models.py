# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Download and export AI models for License Plate Recognition.

Run this script once before starting the pipeline application:
    python3 export_models.py
"""

import json
import shutil
import subprocess
import sys
from pathlib import Path

from huggingface_hub import hf_hub_download, snapshot_download

MODELS_DIR = Path(__file__).resolve().parent / "models"


# ── Model Export Functions ────────────────────────────────────────────────────


def export_yolo_detection(repo_id: str, pt_filename: str) -> Path:
    """Download a YOLO .pt from HuggingFace and export to OpenVINO IR INT8."""
    model_stem = pt_filename.replace(".pt", "")
    ov_dir = MODELS_DIR / f"{model_stem}_openvino"

    xml_files = list(ov_dir.glob("*.xml")) if ov_dir.exists() else []
    if xml_files:
        print(f"[YOLO] Model already exists: {xml_files[0]}")
        return xml_files[0]

    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    print(f"[YOLO] Downloading {repo_id} / {pt_filename}...")
    pt_path = hf_hub_download(
        repo_id=repo_id, filename=pt_filename, local_dir=str(MODELS_DIR)
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


def export_paddleocr(model_id: str) -> Path:
    """Download PaddleOCR model and convert PIR → ONNX → OpenVINO IR FP16."""
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
    snapshot_download(repo_id=model_id, local_dir=str(paddle_dir))

    # Step 2: PaddlePaddle PIR → ONNX
    onnx_file = ocr_dir / "model.onnx"
    print("[OCR] Converting PaddlePaddle PIR → ONNX...")
    subprocess.run(
        [
            "paddle2onnx",
            "--model_dir", str(paddle_dir),
            "--model_filename", "inference.json",
            "--params_filename", "inference.pdiparams",
            "--save_file", str(onnx_file),
            "--opset_version", "14",
        ],
        check=True,
    )

    # Step 3: ONNX → OpenVINO IR FP16
    fp16_dir.mkdir(parents=True, exist_ok=True)
    print("[OCR] Converting ONNX → OpenVINO IR (FP16)...")
    subprocess.run(
        ["ovc", str(onnx_file), "--output_model", str(ov_model), "--compress_to_fp16"],
        check=True,
    )

    # Step 4: Extract character dictionary from config.json
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


# ── CLI ───────────────────────────────────────────────────────────────────────


def main():
    det = export_yolo_detection(
        "morsetechlab/yolov11-license-plate-detection",
        "license-plate-finetune-v1s.pt",
    )
    ocr = export_paddleocr("PaddlePaddle/PP-OCRv5_server_rec")

    print(f"\n=== All models ready ===")
    print(f"  Detection: {det}")
    print(f"  OCR:       {ocr}")


if __name__ == "__main__":
    main()
