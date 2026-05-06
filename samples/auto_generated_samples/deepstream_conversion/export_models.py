# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Download and export YOLO11n detection model for the metapublish sample.

Run this script once before starting the pipeline application:
    python3 export_models.py
"""

import shutil
from pathlib import Path

from ultralytics import YOLO

MODELS_DIR = Path(__file__).resolve().parent / "models"


def export_yolo_detection() -> Path:
    """Download YOLO11n and export to OpenVINO IR with INT8 quantization."""
    model_stem = "yolo11n"
    ov_dir = MODELS_DIR / f"{model_stem}_openvino"

    xml_files = list(ov_dir.glob("*.xml")) if ov_dir.exists() else []
    if xml_files:
        print(f"[YOLO] Model already exists: {xml_files[0]}")
        return xml_files[0]

    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    pt_path = MODELS_DIR / f"{model_stem}.pt"
    print(f"[YOLO] Downloading {model_stem}...")
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
    export_yolo_detection()
