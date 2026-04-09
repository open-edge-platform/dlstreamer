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
from openvino import PartialShape
from openvino import Type
from openvino import save_model
from openvino.tools.ovc import convert_model
from optimum.exporters.onnx import main_export
from transformers import CLIPVisionModel
from transformers import AutoConfig
from transformers import AutoProcessor
from PIL import Image

SUPPORTED_HF_MODELS = {
    "vitforimageclassification",
    "InternVLChatModel",
    "LlavaForConditionalGeneration",
    "LlavaQwen2ForCausalLM",
    "BunnyQwenForCausalLM",
    "LlavaNextForConditionalGeneration",
    "LlavaNextVideoForConditionalGeneration",
    "MiniCPMO",
    "openbmb/MiniCPM-V-2_6",
    "openbmb/MiniCPM-V-4_5",
    "Phi3VForCausalLM",
    "Phi4MMForCausalLM",
    "Qwen2VLForConditionalGeneration",
    "Qwen2_5_VLForConditionalGeneration",
    "google/gemma-3-4b-it",
    "google/gemma-3-12b-it",
    "google/gemma-3-27b-it",
    "WhisperForConditionalGeneration",
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
) -> Path:
    """Export CLIP vision encoder to OpenVINO IR.

    This exports only the visual feature extractor (no text encoder).
    """
    outdir.mkdir(parents=True, exist_ok=True)

    vision_model = CLIPVisionModel.from_pretrained(model_ref)
    vision_model.eval()

    img = Image.new("RGB", (224, 224))
    processor = AutoProcessor.from_pretrained(model_ref, token=token)
    batch = processor.image_processor(images=img, return_tensors="pt")["pixel_values"]

    ov_model = convert_model(vision_model, example_input=batch)

    # Define the input shape explicitly
    input_shape = PartialShape([-1, batch.shape[1], batch.shape[2], batch.shape[3]])

    # Set the input shape and type explicitly
    for nn_input in ov_model.inputs:
        nn_input.get_node().set_partial_shape(PartialShape(input_shape))
        nn_input.get_node().set_element_type(Type.f32)

    ov_model.set_rt_info("clip_token", ["model_info", "model_type"])
    ov_model.set_rt_info("68.500,66.632,70.323", ["model_info", "scale_values"])
    ov_model.set_rt_info("122.771,116.746,104.094", ["model_info", "mean_values"])
    ov_model.set_rt_info("RGB", ["model_info", "color_space"])
    ov_model.set_rt_info("crop", ["model_info", "resize_type"])
    model_name = Path(model_ref).name
    save_model(ov_model, str(outdir / f"{model_name}.xml"))

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
    model_name = Path(model_ref).name
    save_model(ov_model, str(outdir / f"{model_name}.xml"))
    model_onnx.unlink(missing_ok=True)
    return outdir
