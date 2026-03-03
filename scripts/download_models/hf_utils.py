# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Helper functions for Hugging Face model support detection and export."""

from __future__ import annotations

from pathlib import Path
from typing import Callable

from huggingface_hub import hf_hub_download
from openvino import save_model
from openvino.tools.ovc import convert_model
from optimum.exporters.onnx import main_export
from optimum.intel import OVModelForFeatureExtraction
from transformers import AutoConfig
from transformers import AutoProcessor

SUPPORTED_HF_MODELS = {
    "vitforimageclassification",
    "clipmodel",
    "rtdetrforobjectdetection",
    "rtdetrv2forobjectdetection",
    "microsoft/Phi-4-multimodal-instruct",
    "openbmb/MiniCPM-V-2_6",
    "google/gemma-3-4b-it",
}

CUSTOM_CONVERTERS = {
    "clipmodel",
    "rtdetrforobjectdetection",
    "rtdetrv2forobjectdetection",
}


def get_hf_model_support_level(model_id: str, token: str | None = None) -> int:
    """Classify support level for a Hugging Face model ID.

    Returns:
        0: model ID or one of its architectures is in SUPPORTED_HF_MODELS
        1: model ID or one of its architectures is in CUSTOM_CONVERTERS
        2: otherwise
    """
    supported_hf_models_lower = {item.lower() for item in SUPPORTED_HF_MODELS}
    custom_converters_lower = {item.lower() for item in CUSTOM_CONVERTERS}

    normalized_model_id = model_id.strip()
    model_key = normalized_model_id.lower()

    if model_key in supported_hf_models_lower:
        return 0
    if model_key in custom_converters_lower:
        return 1

    try:
        architectures = load_hf_architectures_from_repo(normalized_model_id, token)
    except ValueError:
        return 2

    normalized_architectures = {architecture.lower() for architecture in architectures}
    if normalized_architectures & supported_hf_models_lower:
        return 0
    if normalized_architectures & custom_converters_lower:
        return 1
    return 2


def custom_conversion(
    model_id: str,
    outdir: Path,
    token: str | None,
    extra_args: list[str] | None = None,
) -> Path:
    """Run custom conversion for architectures listed in CUSTOM_CONVERTERS."""
    if extra_args is None:
        extra_args = []

    if model_id.lower() in CUSTOM_CONVERTERS:
        primary_arch = model_id.lower()
    else:
        architectures = load_hf_architectures_from_repo(model_id, token)
        primary_arch = architectures[0].lower()

    export_dir = outdir / Path(model_id).name
    handlers: dict[str, tuple[str, Callable[[], Path]]] = {
        "clipmodel": (
            "a CLIP model",
            lambda: export_hf_clip_to_openvino(
                model_id,
                export_dir,
                token,
                extra_args=extra_args,
            ),
        ),
        "rtdetrforobjectdetection": (
            "an RT-DETR model",
            lambda: export_hf_rtdetr_to_openvino(
                model_id,
                export_dir,
                token,
                extra_args=extra_args,
            ),
        ),
        "rtdetrv2forobjectdetection": (
            "an RT-DETR model",
            lambda: export_hf_rtdetr_to_openvino(
                model_id,
                export_dir,
                token,
                extra_args=extra_args,
            ),
        ),
    }

    model_description, export_handler = handlers[primary_arch]
    print(f"Model {model_id} is {model_description}")
    return export_handler()


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

def export_hf_clip_to_openvino(
    model_ref: str,
    outdir: Path,
    token: str | None,
    extra_args: list[str] | None = None,
) -> Path:
    """Export CLIP vision encoder to OpenVINO IR.

    This exports only the visual feature extractor (no text encoder).
    """
    outdir.mkdir(parents=True, exist_ok=True)
    _ = extra_args
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
    model_ref: str,
    outdir: Path,
    token: str | None,
    extra_args: list[str] | None = None,
) -> Path:
    """Export RT-DETR via PyTorch -> ONNX -> OpenVINO IR.

    Requires `optimum`, `huggingface_hub`, and `openvino` to be installed.
    """
    outdir.mkdir(parents=True, exist_ok=True)
    _ = extra_args
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
