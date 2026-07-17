# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Hugging Face model download/export helpers."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from huggingface_hub import snapshot_download
from hf_utils import custom_conversion
from hf_utils import get_hf_model_support_level
from hf_utils import resolve_hf_model_ref


PRECISION_DIRS = {"FP32", "FP16", "INT8"}


def _precision_from_element_type(element_type: str) -> str | None:
    normalized = element_type.strip().lower()
    if normalized in {"f32", "float32"}:
        return "FP32"
    if normalized in {"f16", "float16"}:
        return "FP16"
    if normalized in {"i8", "u8", "int8", "uint8"}:
        return "INT8"
    return None


def detect_xml_precision(xml_path: Path) -> str:
    tree = ET.parse(xml_path)
    root = tree.getroot()

    for port in root.findall(".//port"):
        precision = (port.attrib.get("precision") or "").strip().upper()
        if precision in PRECISION_DIRS:
            return precision

    for data_node in root.findall(".//data"):
        mapped = _precision_from_element_type(data_node.attrib.get("element_type", ""))
        if mapped:
            return mapped

    # Safe default for OpenVINO exports when metadata is incomplete.
    return "FP32"


def normalize_export_layout(model_path: Path) -> Path:
    xml_candidates = sorted(model_path.rglob("*.xml"), key=lambda p: (len(p.parts), str(p)))
    if not xml_candidates:
        return model_path

    xml_path = xml_candidates[0]
    precision = detect_xml_precision(xml_path)
    current_dir = xml_path.parent

    if current_dir.name in PRECISION_DIRS:
        if current_dir.name == precision:
            return current_dir
        target_dir = current_dir.parent / precision
    else:
        target_dir = model_path / precision

    target_dir.mkdir(parents=True, exist_ok=True)

    if current_dir != target_dir:
        for item in list(current_dir.iterdir()):
            # Avoid moving the precision folder into itself (e.g. FP32 -> FP32/FP32).
            if item == target_dir:
                continue
            destination = target_dir / item.name
            if destination.exists():
                if destination.is_dir() and item.is_dir():
                    continue
                if destination.is_file() and item.is_file():
                    destination.unlink()
                else:
                    continue
            shutil.move(str(item), str(destination))

        if current_dir.exists() and not any(current_dir.iterdir()) and current_dir != model_path:
            current_dir.rmdir()

    return target_dir


def parse_args() -> argparse.Namespace:
    raw_argv = sys.argv[1:]
    script_options = {"-h", "--help", "--model", "--outdir", "--token", "--extra_args"}
    filtered_argv: list[str] = []
    extracted_extra_args: list[str] = []

    i = 0
    while i < len(raw_argv):
        token = raw_argv[i]
        if token == "--extra_args":
            i += 1
            while i < len(raw_argv) and raw_argv[i] not in script_options:
                extracted_extra_args.append(raw_argv[i])
                i += 1
            continue

        filtered_argv.append(token)
        i += 1

    parser = argparse.ArgumentParser(
        description="Download Hugging Face models and convert them to OpenVINO format."
    )
    parser.add_argument(
        "--model",
        required=True,
        help="Hugging Face model ID or repo_id@revision override",
    )
    parser.add_argument(
        "--outdir",
        default=".",
        help="Output directory for exports",
    )
    parser.add_argument(
        "--token",
        default=None,
        help="Hugging Face token for gated/private models",
    )
    parser.add_argument(
        "--extra_args",
        nargs="*",
        default=[],
        help="Additional arguments to pass to optimum-cli export",
    )

    args = parser.parse_args(filtered_argv)
    if extracted_extra_args:
        args.extra_args.extend(extracted_extra_args)
    return args


def main() -> int:
    args = parse_args()
    model_id = args.model
    token = args.token

    try:
        support_level = get_hf_model_support_level(model_id, token)
        repo_id, revision = resolve_hf_model_ref(model_id, token)
        if "@" not in model_id:
            print(f"Resolved {repo_id} to revision {revision}")
        
        match support_level:
            case 0:
                # Standard export using optimum-cli.
                # snapshot_download pins the model to the exact resolved commit SHA
                # so that every CI run exports the same weights (security + reproducibility).
                local_model_dir = snapshot_download(
                    repo_id=repo_id,
                    revision=revision,
                    token=token,
                )

                model_path = Path(args.outdir) / Path(repo_id).name
                model_path.mkdir(parents=True, exist_ok=True)

                command = [
                    "optimum-cli",
                    "export",
                    "openvino",
                    "--model",
                    local_model_dir,
                ]
                if args.extra_args:
                    command.extend(args.extra_args)
                command.append(str(model_path))
                env = os.environ if not token else {**os.environ, "HF_TOKEN": token}

                subprocess.run(command, check=True, env=env)
                model_path = normalize_export_layout(model_path)

            case 1:
                # Custom conversion
                model_path = custom_conversion(
                    model_id,
                    Path(args.outdir),
                    token,
                    extra_args=args.extra_args,
                )
                model_path = normalize_export_layout(model_path)

            case 2:
                print(f"Model is not supported by DL Streamer: {model_id}")
                return 1
            case _:
                raise ValueError(f"Unexpected support level: {support_level}")

        print(f"Exported model location: {model_path}")
        return 0
        
    except OSError as exc:
        print(f"File system or access error while exporting model '{model_id}'")
        print(f"Details: {str(exc)}")
        return 1
    except subprocess.CalledProcessError as exc:
        print(f"Error during model export: {str(exc)}")
        return 1
    except Exception as exc:
        print(f"Unexpected error: {str(exc)}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
