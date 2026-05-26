#!/usr/bin/env python3
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Download and convert HuggingFace face-detection model to OpenVINO IR format.

Detection model:  arnabdhar/YOLOv8-Face-Detection

Usage (standalone):
    python3 prepare_models.py

Usage (from shell — prints KEY=VALUE lines for eval):
    eval "$(python3 prepare_models.py)"
"""

import os
import sys

# Disable Xet storage backend — it fails behind corporate proxies (e.g. Fortinet)
os.environ.setdefault("HF_HUB_DISABLE_XET", "1")

from huggingface_hub import hf_hub_download
from ultralytics import YOLO


def get_runtime_dir():
    """Return target directory for model storage.

    If the ``MODELS_PATH`` environment variable is set, models are stored
    in ``$MODELS_PATH/<sample_folder_name>`` (created if missing).  Otherwise
    they are stored in a ``models`` subdirectory next to this script.
    """
    sample_dir = os.path.dirname(os.path.abspath(__file__))
    models_path = os.environ.get("MODELS_PATH")
    if models_path:
        target = os.path.join(models_path, os.path.basename(sample_dir))
    else:
        target = os.path.join(sample_dir, "models")
    os.makedirs(target, exist_ok=True)
    return target


def _is_ir_model_ready(xml_path):
    """Return True if both .xml and .bin exist and are non-empty."""
    bin_path = os.path.splitext(xml_path)[0] + ".bin"
    return all(
        os.path.isfile(p) and os.path.getsize(p) > 0 for p in (xml_path, bin_path)
    )


def prepare_detection_model():
    """Download YOLOv8-Face-Detection and export to OpenVINO IR."""
    runtime_dir = get_runtime_dir()
    ov_model_path = os.path.join(runtime_dir, "model_openvino_model", "model.xml")

    if _is_ir_model_ready(ov_model_path):
        print(f"Detection model already present: {ov_model_path}", file=sys.stderr)
        return ov_model_path

    print(
        "\nDownloading the detection model and converting to OpenVINO IR format...\n",
        file=sys.stderr,
    )
    model_path = hf_hub_download(
        repo_id="arnabdhar/YOLOv8-Face-Detection",
        filename="model.pt",
        local_dir=runtime_dir,
    )
    model = YOLO(str(model_path))
    exported_model_path = model.export(format="openvino", dynamic=False, imgsz=640)
    print(f"Model exported to {exported_model_path}\n", file=sys.stderr)

    return ov_model_path


def main():
    """Prepare detection model and print path as KEY=VALUE for shell eval."""
    # Redirect fd 1 (stdout) to fd 2 (stderr) at OS level so that ALL output
    # from subprocesses and C libraries goes to stderr.  Only the final
    # KEY=VALUE lines are written to the real stdout for shell eval.
    real_stdout_fd = os.dup(1)
    os.dup2(2, 1)
    sys.stdout = sys.stderr

    detect_path = prepare_detection_model()

    # Restore real stdout for KEY=VALUE output
    os.dup2(real_stdout_fd, 1)
    os.close(real_stdout_fd)
    sys.stdout = os.fdopen(1, "w")

    print(f"DETECT_MODEL_PATH={detect_path}")


if __name__ == "__main__":
    main()
