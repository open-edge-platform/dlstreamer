# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Download and export AI models for Smart NVR.

Run this script once before starting the pipeline application:
    python3 export_models.py
"""

import shutil
import sys
from pathlib import Path

from huggingface_hub import hf_hub_download

MODELS_DIR = Path(__file__).resolve().parent / "models"


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
    det = export_yolo_detection("ultralytics/yolo11", "yolo11n.pt")
    print(f"\n=== All models ready ===")
    print(f"  Detection: {det}")


if __name__ == "__main__":
    main()
