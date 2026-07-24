# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Hugging Face model download/export helpers."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from huggingface_hub import snapshot_download
import openvino as ov
from hf_utils import custom_conversion
from hf_utils import get_hf_model_support_level
from hf_utils import get_optimum_export_task
from hf_utils import install_model_requirements
from hf_utils import parse_model_ref
from hf_utils import requires_trust_remote_code


# Models that require trust_remote_code flag due to custom code in their repo
MODELS_REQUIRING_TRUST_REMOTE_CODE = {
    "OpenGVLab/InternVL2-1B",
    "qnguyen3/nanoLLaVA",
    "qnguyen3/nanoLLaVA-1.5",
}

# Additional dependencies required by specific models for export
MODEL_DEPENDENCIES = {
    "OpenGVLab/InternVL2-1B": ["einops", "timm", "torchvision"],
    "openbmb/MiniCPM-o-2_6": ["soundfile"],
}

MODELS_REQUIRING_PAD_TOKEN_WORKAROUND = {
    "qnguyen3/nanoLLaVA",
    "qnguyen3/nanoLLaVA-1.5",
}

PRECISION_ALIASES = {
    "INT8": {"INT8", "I8", "U8"},
    "FP16": {"FP16", "F16"},
    "FP32": {"FP32", "F32"},
}

SUPPORTED_WEIGHT_FORMATS = {"fp32", "fp16", "int8"}


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
        help="Hugging Face model ID",
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


def validate_extra_args(extra_args: list[str]) -> None:
    """Reject unsupported explicit weight-format values before export starts."""
    for index, token in enumerate(extra_args):
        if token == "--weight-format":
            if index + 1 >= len(extra_args):
                raise ValueError("Missing value after --weight-format")
            weight_format = extra_args[index + 1].strip().lower()
        elif token.startswith("--weight-format="):
            weight_format = token.split("=", 1)[1].strip().lower()
        else:
            continue

        if weight_format not in SUPPORTED_WEIGHT_FORMATS:
            supported = ", ".join(sorted(SUPPORTED_WEIGHT_FORMATS))
            raise ValueError(
                f"Unsupported --weight-format '{weight_format}'. Supported values: {supported}"
            )


def install_model_dependencies(repo_id: str) -> None:
    """Install hardcoded dependencies for specific models."""
    if repo_id not in MODEL_DEPENDENCIES:
        return
    
    deps = MODEL_DEPENDENCIES[repo_id]
    print(f"Installing dependencies for {repo_id}: {', '.join(deps)}")
    try:
        subprocess.run(
            [sys.executable, "-m", "pip", "install"] + deps,
            check=True,
        )
        print("Dependencies installed successfully")
    except subprocess.CalledProcessError as e:
        print(f"Error installing dependencies: {str(e)}")
        raise


def ensure_export_config_compat(local_model_dir: str | Path, repo_id: str) -> None:
    """Apply safe local config workarounds required by some remote-code models."""
    if repo_id not in MODELS_REQUIRING_PAD_TOKEN_WORKAROUND:
        return

    config_path = Path(local_model_dir) / "config.json"
    if not config_path.exists():
        return

    with config_path.open(encoding="utf-8") as file:
        config = json.load(file)

    if "pad_token_id" in config:
        return

    fallback_token_id = config.get("eos_token_id") or config.get("bos_token_id")
    if fallback_token_id is None:
        print(f"Warning: cannot infer pad_token_id for {repo_id}")
        return

    config["pad_token_id"] = fallback_token_id
    with config_path.open("w", encoding="utf-8") as file:
        json.dump(config, file, indent=2)
        file.write("\n")
    print(f"Applied config workaround for {repo_id}: set pad_token_id={fallback_token_id}")


def find_exported_xmls(root: Path) -> list[Path]:
    """Return exported OpenVINO XML files that have matching .bin files."""
    return sorted(path for path in root.rglob("*.xml") if path.with_suffix(".bin").exists())


def detect_ir_precision(exported_xml: Path) -> str:
    """Infer the exported OpenVINO precision from XML metadata."""
    root = ET.parse(exported_xml).getroot()
    seen_types: set[str] = set()

    for element in root.iter():
        for attr_name in ("precision", "element_type"):
            attr_value = element.attrib.get(attr_name)
            if attr_value:
                seen_types.add(attr_value.upper())

    for precision in ("INT8", "FP16", "FP32"):
        if PRECISION_ALIASES[precision] & seen_types:
            return precision

    raise RuntimeError(
        f"Unable to infer exported OpenVINO precision from {exported_xml}. "
        f"Seen XML types: {sorted(seen_types)}"
    )


def normalize_export_layout(export_root: Path, final_model_dir: Path, model_name: str) -> Path:
    """Normalize exported IR into DLStreamer layout with precision subdirectory."""
    exported_xmls = find_exported_xmls(export_root)
    if not exported_xmls:
        raise RuntimeError(f"No exported OpenVINO XML files found in {export_root}")
    if len(exported_xmls) > 1:
        shutil.copytree(export_root, final_model_dir, dirs_exist_ok=True)
        print(
            f"Preserving original export layout for {model_name}: found {len(exported_xmls)} "
            "OpenVINO IR files"
        )
        return final_model_dir

    exported_xml = exported_xmls[0]
    precision_dir = final_model_dir / detect_ir_precision(exported_xml)
    target_xml = precision_dir / f"{model_name}.xml"
    target_bin = target_xml.with_suffix(".bin")

    precision_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=f".{model_name}-", dir=precision_dir) as tmp_dir:
        tmp_xml = Path(tmp_dir) / target_xml.name
        tmp_bin = tmp_xml.with_suffix(".bin")
        shutil.copy2(exported_xml, tmp_xml)
        shutil.copy2(exported_xml.with_suffix(".bin"), tmp_bin)
        ov.Core().read_model(str(tmp_xml))
        tmp_bin.replace(target_bin)
        tmp_xml.replace(target_xml)

    skip_names = {exported_xml.name, exported_xml.with_suffix(".bin").name}
    for companion in exported_xml.parent.iterdir():
        if companion.is_file() and companion.name not in skip_names:
            shutil.copy2(companion, precision_dir / companion.name)

    return precision_dir


def main() -> int:
    args = parse_args()
    model_id = args.model
    token = args.token

    try:
        validate_extra_args(args.extra_args)

        # Parse model_id to extract repo_id and optional revision
        repo_id, revision = parse_model_ref(model_id)
        
        # Download model with specified revision (or latest if None) to local cache
        print(f"Downloading model: {repo_id}" + (f" @ {revision}" if revision else " (latest)"))
        local_model_dir = snapshot_download(
            repo_id=repo_id,
            revision=revision,
            token=token,
        )
        print(f"Model cached at: {local_model_dir}")
        
        # Install model-specific dependencies
        install_model_dependencies(repo_id)
        
        # Install model requirements if they exist
        install_model_requirements(local_model_dir)

        # Apply local config workarounds for known problematic models
        ensure_export_config_compat(local_model_dir, repo_id)
        
        # Determine support level by analyzing locally cached model
        support_level = get_hf_model_support_level(local_model_dir)
        model_name = repo_id.replace("/", "_")
        output_root = Path(args.outdir)
        output_root.mkdir(parents=True, exist_ok=True)
        final_model_dir = output_root / model_name

        with tempfile.TemporaryDirectory(prefix=f".{model_name}-", dir=output_root) as tmp_dir:
            export_root = Path(tmp_dir)

            match support_level:
                case 0:
                    # Standard export using optimum-cli
                    command = [
                        "optimum-cli",
                        "export",
                        "openvino",
                        "--model",
                        local_model_dir,
                    ]
                    export_task = get_optimum_export_task(local_model_dir)
                    if export_task:
                        command.extend(["--task", export_task])
                    if repo_id in MODELS_REQUIRING_TRUST_REMOTE_CODE or requires_trust_remote_code(local_model_dir):
                        command.append("--trust-remote-code")
                    if args.extra_args:
                        command.extend(args.extra_args)
                    command.append(str(export_root))
                    env = os.environ if not token else {**os.environ, "HF_TOKEN": token}

                    subprocess.run(command, check=True, env=env)

                case 1:
                    # Custom conversion using locally cached model
                    custom_conversion(
                        local_model_dir,
                        repo_id,
                        export_root,
                        token,
                        extra_args=args.extra_args,
                    )

                case 2:
                    print(f"Model is not supported by DL Streamer: {model_id}")
                    return 1
                case _:
                    raise ValueError(f"Unexpected support level: {support_level}")

            model_path = normalize_export_layout(export_root, final_model_dir, model_name)

        print(f"Exported model location: {model_path}")
        return 0
        
    except OSError as exc:
        print(f"Error: Model '{model_id}' not found or inaccessible")
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
