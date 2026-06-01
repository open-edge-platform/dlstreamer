# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Download and export YOLO detection model for deepstream_test4_conversion.

Run this script once before starting the pipeline application:
    python3 export_models.py
"""

import shutil
from pathlib import Path

MODELS_DIR = Path(__file__).resolve().parent / "models"


def export_yolo_detection() -> Path:
    """Download yolo11n and export to OpenVINO IR with INT8 quantization."""
    model_stem = "yolo11n"
    ov_dir = MODELS_DIR / f"{model_stem}_openvino"

    xml_files = list(ov_dir.glob("*.xml")) if ov_dir.exists() else []
    if xml_files:
        print(f"[YOLO] Model already exists: {xml_files[0]}")
        return xml_files[0]

    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    print("[YOLO] Downloading yolo11n...")
    from ultralytics import YOLO

    pt_path = MODELS_DIR / f"{model_stem}.pt"
    model = YOLO(str(pt_path))

    print("[YOLO] Exporting to OpenVINO IR (INT8)...")
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


if __name__ == "__main__":
    model_path = export_yolo_detection()
    print(f"\nModel exported to: {model_path}")
