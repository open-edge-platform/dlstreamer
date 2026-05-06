# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Download and export AI models for multi_stream_compose.

Run this script once before starting the pipeline application:
    python3 export_models.py
"""

import shutil
from pathlib import Path

from ultralytics import YOLO

MODELS_DIR = Path(__file__).resolve().parent / "models"


def export_yolo_detection(model_name: str = "yolo11s") -> Path:
    """Download YOLO11s and export to OpenVINO IR INT8."""
    ov_dir = MODELS_DIR / f"{model_name}_openvino"

    xml_files = list(ov_dir.glob("*.xml")) if ov_dir.exists() else []
    if xml_files:
        print(f"[YOLO] Model already exists: {xml_files[0]}")
        return xml_files[0]

    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    pt_path = MODELS_DIR / f"{model_name}.pt"
    print(f"[YOLO] Downloading {model_name}...")
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


def main():
    export_yolo_detection("yolo11s")
    print("\n=== All models ready ===")


if __name__ == "__main__":
    main()
