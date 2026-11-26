# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Download and export AI models for cv_triggered_vlm.

Exports:
  - YOLOv11s object detection model (INT8, OpenVINO IR)
  - InternVL3_5-2B Vision-Language Model (INT4, OpenVINO IR)

Run this script once before starting the pipeline:
    python3 export_models.py
"""

import shutil
import sys
from pathlib import Path
from subprocess import run

MODELS_DIR = Path(__file__).resolve().parent / "models"


def export_yolo_detection() -> Path:
    """Export YOLOv11s to OpenVINO IR with INT8 quantization."""
    ov_dir = MODELS_DIR / "yolo11s_openvino"
    xml_files = list(ov_dir.glob("*.xml")) if ov_dir.exists() else []
    if xml_files:
        print(f"[YOLO] Model already exists: {xml_files[0]}")
        return xml_files[0]

    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    print("[YOLO] Downloading and exporting yolo11s to OpenVINO IR (INT8)...")

    from ultralytics import YOLO

    pt_path = MODELS_DIR / "yolo11s.pt"
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


def export_vlm() -> Path:
    """Export InternVL3_5-2B to OpenVINO IR with INT4 weight compression."""
    model_id = "OpenGVLab/InternVL3_5-2B"
    model_name = "InternVL3_5-2B"
    output_dir = MODELS_DIR / model_name

    if output_dir.exists() and any(output_dir.glob("*.xml")):
        print(f"[VLM] Model already exists: {output_dir}")
        return output_dir

    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    print(f"[VLM] Exporting {model_id} via optimum-cli (int4)...")

    optimum_cli = shutil.which("optimum-cli")
    if not optimum_cli:
        raise FileNotFoundError("Required tool 'optimum-cli' not found in PATH")

    run(
        [
            optimum_cli, "export", "openvino",
            "--model", model_id,
            "--task", "image-text-to-text",
            "--trust-remote-code",
            "--weight-format", "int4",
            str(output_dir),
        ],
        check=True,
    )

    print(f"[VLM] Model ready: {output_dir}")
    return output_dir


def main():
    export_yolo_detection()
    export_vlm()
    print("\n=== All models ready ===")


if __name__ == "__main__":
    main()
