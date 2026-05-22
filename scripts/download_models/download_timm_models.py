#!/usr/bin/env python3
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Export Hugging Face-hosted TIMM image-classification models to OpenVINO IR."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import openvino as ov
import timm


PRECISIONS = ("fp16", "int8", "both")
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


def parse_args() -> argparse.Namespace:
    """Parse CLI arguments."""
    parser = argparse.ArgumentParser(
        description="Export Hugging Face-hosted TIMM models to OpenVINO IR."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list-models")
    list_parser.set_defaults(func=list_models)

    import_parser = subparsers.add_parser("import")
    import_parser.add_argument("--model", required=True)
    import_parser.add_argument("--precision", default="fp16", choices=PRECISIONS)
    import_parser.add_argument("--output-dir", default=None)
    import_parser.set_defaults(func=import_model)
    return parser.parse_args()


def list_models(_args: argparse.Namespace) -> int:
    """Print TIMM model names that expose a Hugging Face model id."""
    models = supported_models()
    print(f"Supported TIMM model names ({len(models)}):")
    for name in models:
        print(f"  {name}")
    print("\nSupported precision: fp16, int8, both")
    return 0


def import_model(args: argparse.Namespace) -> int:
    """Export one TIMM model through Optimum OpenVINO."""
    model_name = args.model.strip()
    precisions = ("fp16", "int8") if args.precision == "both" else (args.precision,)
    output_root = Path(args.output_dir or os.environ.get("MODELS_PATH", "")).expanduser()
    hf_model_id = hf_id(model_name)

    assert hf_model_id, f"unsupported model or missing Hugging Face id: {model_name}"
    assert output_root, "--output-dir is required when MODELS_PATH is not set"
    assert shutil.which("optimum-cli"), "optimum-cli is required in the export environment"

    for precision in precisions:
        xml = output_root / "public" / model_name / precision.upper() / f"{model_name}.xml"
        export_with_optimum(hf_model_id, model_name, precision, xml)
        print(f"Exported {precision.upper()} OpenVINO IR: {xml}")
    return 0


def supported_models() -> list[str]:
    """Return relevant supported models available in the installed TIMM package."""
    timm_names = set(timm.list_models())
    return [name for name in SUPPORTED_MODELS if name in timm_names and hf_id(name)]


def hf_id(model_name: str) -> str | None:
    """Return the Hugging Face id advertised by TIMM for a model name."""
    if model_name not in SUPPORTED_MODELS:
        return None
    cfg = timm.get_pretrained_cfg(model_name)
    return cfg.hf_hub_id if cfg else None


def export_with_optimum(hf_model_id: str, model_name: str, precision: str, xml: Path) -> None:
    """Run optimum-cli, then normalize the output into DLStreamer layout."""
    with tempfile.TemporaryDirectory(prefix=f".{model_name}-{precision}-") as tmp:
        tmpdir = Path(tmp)
        # xet-backed Hugging Face transfers can hang for some TIMM weight files
        env = {**os.environ, "HF_HUB_DISABLE_XET": "1"}
        subprocess.run(
            [
                "optimum-cli",
                "export",
                "openvino",
                "--library",
                "timm",
                "--task",
                "image-classification",
                "--model",
                hf_model_id,
                "--weight-format",
                precision,
                str(tmpdir),
            ],
            env=env,
            check=True,
        )
        exported_xml = only_xml(tmpdir)
        model = ov.Core().read_model(str(exported_xml))
        add_model_info(model, model_name)
        save_ir(model, xml, compress_to_fp16=precision == "fp16")


def only_xml(root: Path) -> Path:
    """Return the single OpenVINO XML file produced by optimum-cli."""
    xmls = sorted(root.rglob("*.xml"))
    assert len(xmls) == 1, f"expected one exported XML in {root}, found {len(xmls)}"
    return xmls[0]


def add_model_info(model: ov.Model, model_name: str) -> None:
    """Attach DLStreamer classification metadata to OpenVINO IR."""
    cfg = timm.get_pretrained_cfg(model_name)
    assert cfg, f"missing TIMM pretrained config: {model_name}"
    mean = to_pixel_values(cfg.mean)
    std = to_pixel_values(cfg.std)
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


def save_ir(model: ov.Model, xml: Path, compress_to_fp16: bool) -> None:
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
