# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Download and export AI models for People Detection & Tracking.

Models:
  - YOLO26m: Person detection (Ultralytics → OpenVINO IR INT8)
  - Mars-Small-128: Person re-identification for DeepSORT tracking

Run this script once before starting the pipeline:
    python3 export_models.py
"""

import argparse
import shutil
import subprocess
import sys
import urllib.request
import logging
from pathlib import Path

import numpy as np

MODELS_DIR = Path(__file__).resolve().parent / "models"

logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")
logger = logging.getLogger(__name__)


# ── YOLO26m Detection Model ─────────────────────────────────────────────────


def export_yolo26m() -> Path:
    """Export YOLO26m to OpenVINO IR with INT8 quantization."""
    model_stem = "yolo26m"
    ov_dir = MODELS_DIR / f"{model_stem}_openvino"

    xml_files = list(ov_dir.glob("*.xml")) if ov_dir.exists() else []
    if xml_files:
        logger.info(f"[YOLO26m] Model already exists: {xml_files[0]}")
        return xml_files[0]

    MODELS_DIR.mkdir(parents=True, exist_ok=True)

    logger.info("[YOLO26m] Downloading and exporting to OpenVINO IR (INT8)...")
    pt_path = MODELS_DIR / "yolo26m.pt"

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
    logger.info(f"[YOLO26m] Model ready: {xml_files[0]}")
    return xml_files[0]


# ── Mars-Small-128 Re-ID Model ──────────────────────────────────────────────


def download_model_py():
    """Download official model.py from deep_sort_pytorch repository."""
    model_py_url = "https://raw.githubusercontent.com/ZQPei/deep_sort_pytorch/master/deep_sort/deep/model.py"
    model_py_path = Path(__file__).parent / "model.py"

    logger.info("Downloading model.py from deep_sort_pytorch repository...")
    urllib.request.urlretrieve(model_py_url, model_py_path)
    logger.info("Downloaded model.py")
    return model_py_path


def export_mars_small128() -> Path:
    """Download Mars-Small128 and convert to OpenVINO IR FP32."""
    output_dir = MODELS_DIR / "mars-small128"
    model_xml = output_dir / "mars_small128_fp32.xml"

    if model_xml.exists():
        logger.info(f"[Mars-Small128] Model already exists: {model_xml}")
        return model_xml

    output_dir.mkdir(parents=True, exist_ok=True)

    import torch
    import openvino as ov

    # Download model architecture
    model_py_path = download_model_py()
    sys.path.insert(0, str(model_py_path.parent))
    from model import BasicBlock, make_layers

    class NetOriginal(torch.nn.Module):
        def __init__(self, num_classes=625, reid=False):
            super().__init__()
            self.conv = torch.nn.Sequential(
                torch.nn.Conv2d(3, 32, 3, stride=1, padding=1),
                torch.nn.BatchNorm2d(32),
                torch.nn.ReLU(inplace=True),
                torch.nn.Conv2d(32, 32, 3, stride=1, padding=1),
                torch.nn.BatchNorm2d(32),
                torch.nn.ReLU(inplace=True),
                torch.nn.MaxPool2d(3, 2, padding=1),
            )
            self.layer1 = make_layers(32, 32, 2, False)
            self.layer2 = make_layers(32, 64, 2, True)
            self.layer3 = make_layers(64, 128, 2, True)
            self.dense = torch.nn.Sequential(
                torch.nn.Dropout(p=0),
                torch.nn.Linear(128 * 16 * 8, 128),
                torch.nn.BatchNorm1d(128),
                torch.nn.ReLU(inplace=True),
            )
            self.batch_norm = torch.nn.BatchNorm1d(128)
            self.reid = reid
            self.classifier = torch.nn.Sequential(
                torch.nn.Linear(128, num_classes),
            )

        def forward(self, x):
            x = self.conv(x)
            x = self.layer1(x)
            x = self.layer2(x)
            x = self.layer3(x)
            x = x.view(x.size(0), -1)
            x = self.dense(x)
            x = self.batch_norm(x)
            if self.reid:
                x = x.div(x.norm(p=2, dim=1, keepdim=True))
                return x
            x = self.classifier(x)
            return x

    # Download checkpoint from Google Drive
    checkpoint_url = "https://drive.google.com/uc?id=1lfCXBm5ltH-6CjJ1a5rqiZoWgGmRsZSY"
    checkpoint_path = output_dir / "original_ckpt.t7"

    logger.info("[Mars-Small128] Downloading checkpoint...")
    urllib.request.urlretrieve(checkpoint_url, checkpoint_path)

    # Load model in reid mode
    model = NetOriginal(reid=True)
    checkpoint = torch.load(checkpoint_path, map_location="cpu", weights_only=False)
    if "net_dict" in checkpoint:
        model.load_state_dict(checkpoint["net_dict"])
    else:
        model.load_state_dict(checkpoint)
    model.eval()

    # Convert to OpenVINO FP32
    input_shape = (1, 3, 128, 64)
    example_input = torch.randn(input_shape)
    ov_model = ov.convert_model(model, example_input=example_input, input=[("x", input_shape)])
    ov_model.reshape({"x": input_shape})
    ov.save_model(ov_model, str(model_xml))

    # Verify
    core = ov.Core()
    compiled = core.compile_model(core.read_model(model_xml), "CPU")
    test_input = np.random.randn(*input_shape).astype(np.float32)
    output = compiled([test_input])[0]
    logger.info(f"[Mars-Small128] Output shape: {output.shape}, L2 norm: {np.linalg.norm(output):.4f}")

    # Cleanup
    checkpoint_path.unlink(missing_ok=True)
    model_py_path.unlink(missing_ok=True)

    logger.info(f"[Mars-Small128] Model ready: {model_xml}")
    return model_xml


# ── CLI ──────────────────────────────────────────────────────────────────────


def main():
    parser = argparse.ArgumentParser(description="Export models for People Detection & Tracking")
    parser.parse_args()

    export_yolo26m()
    export_mars_small128()

    logger.info("\n=== All models ready ===")


if __name__ == "__main__":
    main()
