#!/usr/bin/env python3
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Export a validated subset of TIMM image-classification models to OpenVINO IR."""

from __future__ import annotations

import argparse
import os
import tempfile
from pathlib import Path

import nncf
import numpy as np
import openvino as ov
import timm
import torch
from PIL import Image


SUPPORTED_MODELS = (
    "densenet121",
    "dla34",
    "dm_nfnet_f0",
    "efficientnet_b0",
    "efficientnetv2_rw_s",
    "inception_resnet_v2",
    "mixnet_l",
    "mobilenetv2_100",
    "mobilenetv3_large_100",
    "mobilenetv3_small_100",
    "regnetx_032",
    "repvgg_a0",
    "repvgg_b1",
    "repvgg_b3",
    "resnest50d",
    "resnet18",
    "resnet34",
    "resnet50",
    "rexnet_100",
    "semnasnet_100",
    "seresnet50",
    "seresnext50_32x4d",
    "swin_tiny_patch4_window7_224",
    "tf_efficientnet_b0",
    "tf_efficientnetv2_b0",
    "vgg16",
    "vgg19",
)
PRECISIONS = ("fp16", "int8", "both")


def parse_args() -> argparse.Namespace:
    """Parse CLI arguments."""
    parser = argparse.ArgumentParser(
        description="Export validated TIMM models to OpenVINO IR."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list-models")
    list_parser.set_defaults(func=list_models)

    import_parser = subparsers.add_parser("import")
    import_parser.add_argument("--model", required=True)
    import_parser.add_argument("--precision", default="fp16", choices=PRECISIONS)
    import_parser.add_argument("--output-dir", default=None)
    import_parser.add_argument("--calibration-data", default=None)
    import_parser.add_argument("--calibration-subset-size", type=int, default=300)
    import_parser.set_defaults(func=import_model)
    return parser.parse_args()


def list_models(_args: argparse.Namespace) -> int:
    """Print supported models that are present in the installed TIMM package."""
    timm_models = set(timm.list_models())
    models = [name for name in SUPPORTED_MODELS if name in timm_models]
    print(f"Supported TIMM model names ({len(models)}):")
    for name in models:
        print(f"  {name}")
    print("\nSupported precision: fp16, int8, both")
    print("INT8 requires --calibration-data with representative images.")
    return 0


def import_model(args: argparse.Namespace) -> int:
    """Export one TIMM model in FP16, INT8, or both."""
    model_name = args.model.strip()
    precisions = ("fp16", "int8") if args.precision == "both" else (args.precision,)
    output_root = Path(args.output_dir or os.environ.get("MODELS_PATH", "")).expanduser()

    assert model_name in SUPPORTED_MODELS, f"unsupported model: {model_name}"
    assert model_name in set(timm.list_models()), f"model not found in installed TIMM: {model_name}"
    assert output_root, "--output-dir is required when MODELS_PATH is not set"

    calibration_images = []
    if "int8" in precisions:
        calibration_images = collect_calibration_images(
            args.calibration_data,
            args.calibration_subset_size,
        )

    model = timm.create_model(model_name, pretrained=True)
    model.eval()
    cfg = dict(getattr(model, "pretrained_cfg", None) or getattr(model, "default_cfg", None) or {})
    input_size = tuple(int(x) for x in cfg.get("input_size", (3, 224, 224)))
    assert len(input_size) == 3, f"expected CHW input size, got: {input_size}"

    print(f"Converting {model_name} with input shape {(1, *input_size)}")
    with torch.no_grad():
        ov_model = ov.convert_model(
            model,
            example_input=torch.randn((1, *input_size)),
            input=[("x", [1, *input_size])],
        )
    ov_model.reshape({"x": [1, *input_size]})
    add_model_info(ov_model, cfg)

    if "fp16" in precisions:
        xml = output_root / "public" / model_name / "FP16" / f"{model_name}.xml"
        save_ir(ov_model, xml, compress_to_fp16=True)
        print(f"Exported FP16 OpenVINO IR: {xml}")

    if "int8" in precisions:
        print(f"Quantizing {model_name} with {len(calibration_images)} calibration images")
        int8_model = nncf.quantize(
            ov_model,
            nncf.Dataset(preprocess_images(calibration_images, cfg, input_size)),
            subset_size=min(args.calibration_subset_size, len(calibration_images)),
        )
        int8_model.set_rt_info(ov.get_version(), "Runtime_version")
        add_model_info(int8_model, cfg)
        xml = output_root / "public" / model_name / "INT8" / f"{model_name}.xml"
        save_ir(int8_model, xml, compress_to_fp16=False)
        print(f"Exported INT8 OpenVINO IR: {xml}")

    return 0


def collect_calibration_images(directory: str | None, limit: int) -> list[Path]:
    """Collect calibration image paths for INT8."""
    assert directory, "INT8 export requires --calibration-data"
    assert limit > 0, "--calibration-subset-size must be positive"
    root = Path(directory).expanduser()
    assert root.is_dir(), f"calibration directory does not exist: {root}"
    images = sorted(
        p for p in root.rglob("*") if p.suffix.lower() in Image.registered_extensions()
    )
    assert images, f"no calibration images found in: {root}"
    return images[:limit]


def preprocess_images(paths: list[Path], cfg: dict, input_size: tuple[int, int, int]):
    """Yield NCHW FP32 tensors for NNCF calibration."""
    channels, height, width = input_size
    assert channels == 3, "only RGB image-classification models are supported"
    mean = np.asarray(cfg.get("mean", (0.485, 0.456, 0.406)), dtype=np.float32)
    std = np.asarray(cfg.get("std", (0.229, 0.224, 0.225)), dtype=np.float32)

    for path in paths:
        image = Image.open(path).convert("RGB")
        image = image.resize((width, height), Image.Resampling.BICUBIC)
        array = np.asarray(image, dtype=np.float32) / 255.0
        array = (array - mean) / std
        yield np.expand_dims(array.transpose(2, 0, 1), axis=0).astype(np.float32)


def add_model_info(model, cfg: dict) -> None:
    """Attach DL Streamer classification metadata to OpenVINO IR."""
    mean = to_pixel_values(cfg.get("mean", (0.485, 0.456, 0.406)))
    std = to_pixel_values(cfg.get("std", (0.229, 0.224, 0.225)))
    model.set_rt_info("label", ["model_info", "model_type"])
    model.set_rt_info("True", ["model_info", "output_raw_scores"])
    model.set_rt_info("RGB", ["model_info", "color_space"])
    model.set_rt_info("crop", ["model_info", "resize_type"])
    model.set_rt_info(", ".join(f"{x:.6g}" for x in mean), ["model_info", "mean_values"])
    model.set_rt_info(", ".join(f"{x:.6g}" for x in std), ["model_info", "scale_values"])


def to_pixel_values(values) -> tuple[float, float, float]:
    """Convert normalization values from unit scale to pixel scale if needed."""
    values = tuple(float(x) for x in values)
    assert len(values) == 3, f"expected three normalization values, got: {values}"
    if all(0.0 <= x <= 1.0 for x in values):
        return tuple(x * 255.0 for x in values)
    return values


def save_ir(model, xml: Path, compress_to_fp16: bool) -> None:
    """Save IR through a temp dir, verify it, then move it into place."""
    xml.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{xml.stem}-", dir=xml.parent) as tmp:
        tmp_xml = Path(tmp) / xml.name
        ov.save_model(model, str(tmp_xml), compress_to_fp16=compress_to_fp16)
        ov.Core().read_model(str(tmp_xml))
        tmp_xml.with_suffix(".bin").replace(xml.with_suffix(".bin"))
        tmp_xml.replace(xml)


cli_args = parse_args()
cli_args.func(cli_args)
