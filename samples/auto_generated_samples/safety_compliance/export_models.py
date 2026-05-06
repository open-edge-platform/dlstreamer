# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Download and export AI models for Safety Compliance.

Run this script once before starting the pipeline application:
    python3 export_models.py

Models:
  - YOLO26m: person detection (Ultralytics → OpenVINO IR INT8)
  - Qwen2.5-VL-3B-Instruct: VLM safety compliance verification (optimum-cli → OpenVINO IR INT4)
"""

import shutil
import subprocess
import sys
from pathlib import Path

MODELS_DIR = Path(__file__).resolve().parent / "models"


def export_yolo_detection(model_name: str = "yolo26m") -> Path:
    """Export YOLO26m to OpenVINO IR INT8 via Ultralytics."""
    ov_dir = MODELS_DIR / f"{model_name}_int8_openvino_model"
    xml_files = list(ov_dir.glob("*.xml")) if ov_dir.exists() else []
    if xml_files:
        print(f"[YOLO] Model already exists: {xml_files[0]}")
        return xml_files[0]

    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    print(f"[YOLO] Downloading and exporting {model_name} to OpenVINO IR (INT8)...")
    pt_path = MODELS_DIR / f"{model_name}.pt"
    from ultralytics import YOLO
    model = YOLO(str(pt_path))
    exported_path = model.export(format="openvino", dynamic=True, int8=True)

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


def export_vlm(model_id: str = "Qwen/Qwen2.5-VL-3B-Instruct") -> Path:
    """Export Qwen2.5-VL-3B-Instruct to OpenVINO IR INT4 via optimum-cli."""
    model_name = model_id.split("/")[-1]
    model_path = MODELS_DIR / model_name

    if model_path.exists() and list(model_path.glob("*.xml")):
        print(f"[VLM] Model already exists: {model_path}")
        return model_path

    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    print(f"[VLM] Exporting {model_id} via optimum-cli (INT4)...")
    subprocess.run(
        [
            "optimum-cli", "export", "openvino",
            "--model", model_id,
            "--task", "image-text-to-text",
            "--trust-remote-code",
            "--weight-format", "int4",
            str(model_path),
        ],
        check=True,
    )

    print(f"[VLM] Model ready: {model_path}")
    return model_path


def main():
    export_yolo_detection()
    export_vlm()
    print("\n=== All models ready ===")


if __name__ == "__main__":
    main()
