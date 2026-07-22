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
import sys
import tempfile
from pathlib import Path

from huggingface_hub import HfApi, hf_hub_download
import timm
import openvino as ov
from hf_utils import parse_model_ref


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
    model_name, revision = parse_model_ref(args.model)
    precisions = ("fp16", "int8") if args.precision == "both" else (args.precision,)
    output_root = Path(args.output_dir or os.environ.get("MODELS_PATH", "")).expanduser()
    hf_model_id = hf_id(model_name)

    assert hf_model_id, f"unsupported model or missing Hugging Face id: {model_name}"
    assert output_root, "--output-dir is required when MODELS_PATH is not set"
    optimum_cli_path = shutil.which("optimum-cli")
    assert optimum_cli_path, "optimum-cli is required in the export environment"

    for precision in precisions:
        xml = output_root / "public" / model_name / precision.upper() / f"{model_name}.xml"
        export_with_optimum(
            optimum_cli_path,
            hf_model_id,
            model_name,
            precision,
            xml,
            revision=revision,
        )
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


def export_with_optimum(
    optimum_cli_path: str,
    hf_model_id: str,
    model_name: str,
    precision: str,
    xml: Path,
    revision: str | None = None,
) -> None:
    """Run optimum-cli, then normalize the output into DLStreamer layout."""
    with tempfile.TemporaryDirectory(prefix=f".{model_name}-{precision}-") as tmp:
        tmpdir = Path(tmp)
        # xet-backed Hugging Face transfers can hang for some TIMM weight files
        env = {**os.environ, "HF_HUB_DISABLE_XET": "1"}

        base_command = [
            optimum_cli_path,
            "export",
            "openvino",
            "--library",
            "timm",
            "--task",
            "image-classification",
            "--model",
            hf_model_id,
        ]

        # First try revision-aware export. If the installed optimum-cli is too old and
        # does not support --revision, fallback to export without --revision.
        command = list(base_command)
        if revision:
            command.extend(["--revision", revision])
        command.extend(["--weight-format", precision, str(tmpdir)])

        result = subprocess.run(
            command,
            env=env,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            combined_output = (result.stdout or "") + "\n" + (result.stderr or "")
            if revision and "unrecognized arguments: --revision" in combined_output:
                fallback = list(base_command)
                fallback.extend(["--weight-format", precision, str(tmpdir)])
                subprocess.run(fallback, env=env, check=True)
            else:
                # Surface captured output for easier CI debugging.
                if result.stdout:
                    print(result.stdout, end="")
                if result.stderr:
                    print(result.stderr, end="", file=sys.stderr)
                raise subprocess.CalledProcessError(result.returncode, command)

        exported_xml = only_xml(tmpdir)
        save_ir(exported_xml, xml)
        save_config_json(hf_model_id, xml.parent / "config.json", revision=revision)


def only_xml(root: Path) -> Path:
    """Return the single OpenVINO XML file produced by optimum-cli."""
    xmls = sorted(root.rglob("*.xml"))
    assert len(xmls) == 1, f"expected one exported XML in {root}, found {len(xmls)}"
    return xmls[0]


def save_config_json(hf_model_id: str, path: Path, revision: str | None = None) -> None:
    """Copy the model's original Hugging Face config.json next to the exported IR."""
    resolved_revision = revision
    if not resolved_revision:
        resolved_revision = HfApi().model_info(hf_model_id).sha
    if not resolved_revision:
        raise RuntimeError(f"Unable to resolve Hugging Face revision for {hf_model_id}")
    config_path = Path(
        hf_hub_download(
            repo_id=hf_model_id,
            filename="config.json",
            revision=resolved_revision,
        )
    )
    shutil.copy2(config_path, path)


def save_ir(exported_xml: Path, xml: Path) -> None:
    """Verify the exported IR, then move it into the DLStreamer layout unchanged."""
    xml.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{xml.stem}-", dir=xml.parent) as tmp:
        tmp_xml = Path(tmp) / xml.name
        tmp_bin = tmp_xml.with_suffix(".bin")
        shutil.copy2(exported_xml, tmp_xml)
        shutil.copy2(exported_xml.with_suffix(".bin"), tmp_bin)
        ov.Core().read_model(str(tmp_xml))
        tmp_bin.replace(xml.with_suffix(".bin"))
        tmp_xml.replace(xml)
    skip = {exported_xml.name, exported_xml.with_suffix(".bin").name}
    for companion in exported_xml.parent.iterdir():
        if companion.name not in skip and companion.is_file():
            shutil.copy2(companion, xml.parent / companion.name)


if __name__ == "__main__":
    _args = parse_args()
    raise SystemExit(_args.func(_args))
