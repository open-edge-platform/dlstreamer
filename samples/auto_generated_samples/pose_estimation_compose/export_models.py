# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Download and export YOLO pose estimation models to OpenVINO IR format.

Run this script once before starting the pipeline application:
    python3 export_models.py
"""

import shutil
from pathlib import Path

from ultralytics import YOLO

MODELS_DIR = Path(__file__).resolve().parent / "models"

# Models to export: (ultralytics_name, display_name)
POSE_MODELS = [
    ("yolo26n-pose.pt", "yolo26n-pose"),
    ("yolo11n-pose.pt", "yolo11n-pose"),
    ("yolov8n-pose.pt", "yolov8n-pose"),
    ("yolov8l-pose.pt", "yolov8l-pose"),
]


def export_yolo_pose(pt_name: str, model_stem: str) -> Path:
    """Download a YOLO pose .pt and export to OpenVINO IR with INT8 quantization."""
    ov_dir = MODELS_DIR / f"{model_stem}_openvino"

    xml_files = list(ov_dir.glob("*.xml")) if ov_dir.exists() else []
    if xml_files:
        print(f"[POSE] Model already exists: {xml_files[0]}")
        return xml_files[0]

    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    pt_path = MODELS_DIR / pt_name
    print(f"[POSE] Loading {pt_name}...")
    model = YOLO(str(pt_path))

    print(f"[POSE] Exporting {model_stem} to OpenVINO IR (INT8)...")
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
    print(f"[POSE] Model ready: {xml_files[0]}")
    return xml_files[0]


def main():
    for pt_name, model_stem in POSE_MODELS:
        export_yolo_pose(pt_name, model_stem)

    print("\n=== All pose models ready ===")


if __name__ == "__main__":
    main()
