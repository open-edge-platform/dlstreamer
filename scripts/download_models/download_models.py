# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Download models and prepare for IR conversion."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from typing import Callable
from dataclasses import dataclass
from pathlib import Path
from ultralytics import YOLO
from transformers import AutoConfig
from transformers import AutoProcessor
from optimum.intel import OVModelForFeatureExtraction
from huggingface_hub import hf_hub_download
from openvino import save_model
from openvino.tools.ovc import convert_model
from optimum.exporters.onnx import main_export

SUPPORTED_HF_ARCHS = {
    "vitforimageclassification",
    "clipmodel",
    "rtdetrforobjectdetection",
    "rtdetrv2forobjectdetection",
}

SUPPORTED_GENAI_MODELS = {
    "phi4mmforcausallm",
    "openbmb/MiniCPM-V-2_6",
    "google/gemma-3-4b-it",
}


@dataclass(frozen=True)
class ModelSpec:
    model: str
    source: str
    outdir: Path
    token: str | None
    precision: str


def parse_args() -> ModelSpec:
    parser = argparse.ArgumentParser(
        description="Download models and convert them to IR format."
    )
    parser.add_argument(
        "--model",
        required=True,
        help="Model ID or local path (.pt for ultralytics, dir for hf)",
    )
    parser.add_argument(
        "--source",
        required=True,
        choices=("hf", "ultralytics"),
        help="Model source hub (hf or ultralytics)",
    )
    parser.add_argument(
        "--outdir",
        default=".",
        help="Output directory for downloads/exports",
    )
    parser.add_argument(
        "--token",
        default=None,
        help="Hugging Face token for gated/private models",
    )
    parser.add_argument(
        "--precision",
        choices=("fp32", "fp16", "int8"),
        default="fp32",
        help="Ultralytics OpenVINO export precision (fp32, fp16, int8)",
    )

    args = parser.parse_args()
    return ModelSpec(
        model=args.model,
        source=args.source,
        outdir=Path(args.outdir),
        token=args.token,
        precision=args.precision,
    )


def dispatch_model_handler(spec: ModelSpec) -> Path:
    if spec.source == "hf":
        return download_hf_model(spec.model, spec.outdir, spec.token)
    if spec.source == "ultralytics":
        return download_ultralytics_model(
            spec.model,
            spec.outdir,
            precision=spec.precision,
        )

    raise ValueError(f"Unsupported source: {spec.source}")


def download_hf_model(model_id: str, outdir: Path, token: str | None) -> Path:
    """Download or resolve a HuggingFace model."""

    path = Path(model_id)
    if path.exists():
        if not path.is_dir():
            raise ValueError("HuggingFace model path must be a directory")
        config_path = path / "config.json"
        architectures = load_hf_architectures(config_path)
        model_ref = path
    else:
        architectures = load_hf_architectures_from_repo(model_id, token)
        model_ref = model_id

    if model_ref == "openbmb/MiniCPM-V-2_6":
        if not token:
            raise ValueError("MiniCPM-V-2_6 export requires --token")
        export_dir = outdir / "MiniCPM-V-2_6"
        print(f"Model {model_id} is MiniCPM-V-2_6")
        return export_hf_minicpm_v26_to_openvino(export_dir, token)
    if model_ref == "google/gemma-3-4b-it":
        if not token:
            raise ValueError("Gemma3 export requires --token")
        export_dir = outdir / "Gemma3"
        print(f"Model {model_id} is Gemma3")
        return export_hf_gemma3_to_openvino(export_dir, token)

    primary_arch = architectures[0].lower()

    if primary_arch == "phi4mmforcausallm":
        if "phi4mmforcausallm" not in SUPPORTED_GENAI_MODELS:
            raise ValueError("Phi4MM export is not enabled")
        if isinstance(model_ref, Path):
            raise ValueError("Phi4MM export supports only HuggingFace model IDs")
        export_dir = outdir / Path(model_ref).name
        print(f"Model {model_id} is a Phi4MM causal LM")
        return export_hf_phi4mm_to_openvino(model_ref, export_dir, token)

    if not is_supported_hf_arch(architectures):
        raise ValueError(
            "Unsupported HuggingFace architecture: " + ", ".join(architectures)
        )

    return export_supported_hf_architecture(
        primary_arch=primary_arch,
        model_id=model_id,
        model_ref=model_ref,
        outdir=outdir,
        token=token,
    )


def require_hf_model_id(model_ref: str | Path, model_name: str) -> str:
    if isinstance(model_ref, Path):
        raise ValueError(f"{model_name} export supports only HuggingFace model IDs")
    return model_ref


def export_supported_hf_architecture(
    primary_arch: str,
    model_id: str,
    model_ref: str | Path,
    outdir: Path,
    token: str | None,
) -> Path:
    export_dir = outdir / Path(model_ref).name
    handlers: dict[str, tuple[str, Callable[[], Path]]] = {
        "vitforimageclassification": (
            "a ViT for image classification",
            lambda: export_hf_vit_to_openvino(model_ref, export_dir, token),
        ),
        "clipmodel": (
            "a CLIP model",
            lambda: export_hf_clip_to_openvino(
                require_hf_model_id(model_ref, "CLIP"),
                export_dir,
                token,
            ),
        ),
        "rtdetrforobjectdetection": (
            "an RT-DETR model",
            lambda: export_hf_rtdetr_to_openvino(model_ref, export_dir, token),
        ),
        "rtdetrv2forobjectdetection": (
            "an RT-DETR model",
            lambda: export_hf_rtdetr_to_openvino(model_ref, export_dir, token),
        ),
    }

    model_description, export_handler = handlers[primary_arch]
    print(f"Model {model_id} is {model_description}")
    return export_handler()


def load_hf_architectures(config_path: Path) -> list[str]:
    if not config_path.exists():
        raise FileNotFoundError(str(config_path))

    with config_path.open("r", encoding="utf-8") as file:
        config = json.load(file)

    architectures = config.get("architectures")
    if not architectures:
        raise ValueError("HuggingFace config.json has no architectures list")
    if isinstance(architectures, str):
        return [architectures]
    if isinstance(architectures, list):
        return [str(item) for item in architectures]

    raise ValueError("HuggingFace architectures must be a string or list")


def load_hf_architectures_from_repo(
    model_id: str,
    token: str | None,
) -> list[str]:
    config = AutoConfig.from_pretrained(model_id, token=token)
    architectures = getattr(config, "architectures", None)
    if not architectures:
        raise ValueError("HuggingFace config has no architectures list")
    if isinstance(architectures, str):
        return [architectures]
    if isinstance(architectures, list):
        return [str(item) for item in architectures]

    raise ValueError("HuggingFace architectures must be a string or list")


def run_optimum_cli_openvino_export(
    model_ref: str,
    outdir: Path,
    token: str | None,
    *,
    extra_args: list[str] | None = None,
    model_name_for_error: str | None = None,
) -> Path:
    outdir.mkdir(parents=True, exist_ok=True)
    command = [
        "optimum-cli",
        "export",
        "openvino",
        "--model",
        model_ref,
    ]
    if extra_args:
        command.extend(extra_args)
    command.append(str(outdir))

    env = os.environ if not token else {**os.environ, "HF_TOKEN": token}
    try:
        subprocess.run(
            command,
            check=True,
            env=env,
        )
    except FileNotFoundError as exc:
        export_name = model_name_for_error or model_ref
        raise ValueError(f"optimum-cli is required for {export_name} export") from exc

    return outdir


def export_hf_vit_to_openvino(
    model_ref: str | Path,
    outdir: Path,
    token: str | None,
) -> Path:
    return run_optimum_cli_openvino_export(
        model_ref=str(model_ref),
        outdir=outdir,
        token=token,
        model_name_for_error="ViT",
    )


def export_hf_clip_to_openvino(
    model_ref: str,
    outdir: Path,
    token: str | None,
) -> Path:
    """Export CLIP vision encoder to OpenVINO IR.

    This exports only the visual feature extractor (no text encoder).
    """
    outdir.mkdir(parents=True, exist_ok=True)
    kwargs: dict[str, str] = {}
    if token:
        kwargs["token"] = token
    ov_model = OVModelForFeatureExtraction.from_pretrained(
        model_ref,
        export=True,
        **kwargs,
    )
    ov_model.save_pretrained(str(outdir))
    processor = AutoProcessor.from_pretrained(model_ref, token=token)
    processor.save_pretrained(str(outdir))
    return outdir


def export_hf_rtdetr_to_openvino(
    model_ref: str | Path,
    outdir: Path,
    token: str | None,
) -> Path:
    """Export RT-DETR via PyTorch -> ONNX -> OpenVINO IR.

    Requires `optimum`, `huggingface_hub`, and `openvino` to be installed.
    """
    if isinstance(model_ref, Path):
        raise ValueError("RT-DETR export supports only HuggingFace model IDs")

    outdir.mkdir(parents=True, exist_ok=True)
    model_id = model_ref
    model_onnx = outdir / "model.onnx"

    main_export(
        model_id,
        output=outdir,
        task="object-detection",
        opset=18,
        width=640,
        height=640,
        auth_token=token,
    )

    hf_hub_download(
        repo_id=model_id,
        filename="preprocessor_config.json",
        local_dir=str(outdir),
        token=token,
    )

    ov_model = convert_model(str(model_onnx))
    save_model(ov_model, str(outdir / "model.xml"))
    model_onnx.unlink(missing_ok=True)
    return outdir


def export_hf_phi4mm_to_openvino(
    model_ref: str,
    outdir: Path,
    token: str | None,
) -> Path:
    """Export Phi4MMForCausalLM to OpenVINO IR.

    Equivalent to: optimum-cli export openvino --model <id> <outdir>
    """
    return run_optimum_cli_openvino_export(
        model_ref=model_ref,
        outdir=outdir,
        token=token,
        model_name_for_error="Phi4MM",
    )


def export_hf_minicpm_v26_to_openvino(outdir: Path, token: str) -> Path:
    """Export MiniCPM-V-2_6 using optimum-cli with int4 weights."""
    return run_optimum_cli_openvino_export(
        model_ref="openbmb/MiniCPM-V-2_6",
        outdir=outdir,
        token=token,
        extra_args=["--weight-format", "int4"],
        model_name_for_error="MiniCPM-V-2_6",
    )


def export_hf_gemma3_to_openvino(outdir: Path, token: str) -> Path:
    """Export Gemma3 using optimum-cli."""
    return run_optimum_cli_openvino_export(
        model_ref="google/gemma-3-4b-it",
        outdir=outdir,
        token=token,
        model_name_for_error="Gemma3",
    )


def is_supported_hf_arch(architectures: list[str]) -> bool:
    for arch in architectures:
        if arch.lower() in SUPPORTED_HF_ARCHS:
            return True
    return False


def download_ultralytics_model(
    model_or_path: str,
    outdir: Path,
    *,
    precision: str,
) -> Path:
    """Download or resolve an Ultralytics model (.pt)."""
    if precision not in {"fp32", "fp16", "int8"}:
        raise ValueError("Ultralytics precision must be one of: fp32, fp16, int8")

    half = precision == "fp16"
    int8 = precision == "int8"

    outdir.mkdir(parents=True, exist_ok=True)
    path = Path(model_or_path)
    if path.exists():
        if path.suffix.lower() != ".pt":
            raise ValueError("Ultralytics local model must be a .pt file")
        model = YOLO(str(path))
    else:
        model = YOLO(model_or_path)
    try:
        exported_model_path = model.export(
            format="openvino",
            dynamic=True,
            half=half,
            int8=int8,
            project=str(outdir),
        )
    except Exception as exc:
        raise RuntimeError("Ultralytics export failed") from exc

    # Move the exported model to the desired output directory.
    exported_path = Path(exported_model_path)
    desired_path = outdir / exported_path.name
    exported_path.rename(desired_path)
    return desired_path


def main() -> int:
    spec = parse_args()
    try:
        model_path = dispatch_model_handler(spec)
        _ = model_path
        print(f"Exported model location: {model_path}")
    except FileNotFoundError as exc:
        missing = exc.filename or spec.model
        print(f"File not found: {missing}")
        print("Suggestions:")
        print("- Verify the path exists or use an absolute path")
        print("- If using a hub ID, check the spelling and network access")
        return 1
    except ValueError as exc:
        print(str(exc))
        return 1
    except NotImplementedError as exc:
        print(str(exc))
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
