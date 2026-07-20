# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Helper functions for Hugging Face model support detection and export."""

from __future__ import annotations

from pathlib import Path
from typing import Callable
import json
import shutil

from huggingface_hub import hf_hub_download, snapshot_download
from openvino import PartialShape
from openvino import Type
from openvino import save_model
from openvino.tools.ovc import convert_model
from optimum.exporters.onnx import main_export
from transformers import AutoModelForDepthEstimation, CLIPVisionModel
from transformers import AutoConfig
from transformers import AutoProcessor, AutoImageProcessor
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
    "depthanythingfordepthestimation",
}


# Task hints for optimum-cli when exporting from a local snapshot.
# Some models cannot be auto-detected by optimum without explicit --task.
OPTIMUM_TASK_BY_ARCH = {
    "vitforimageclassification": "image-classification",
    "whisperforconditionalgeneration": "automatic-speech-recognition",
}


def parse_model_ref(model_ref: str) -> tuple[str, str | None]:
    """Parse model reference in format 'repo_id@revision' or 'repo_id'.
    
    Returns:
        Tuple of (repo_id, revision) where revision is None if not specified.
    """
    if "@" in model_ref:
        repo_id, revision = model_ref.rsplit("@", 1)
        return repo_id.strip(), revision.strip()
    return model_ref.strip(), None


def load_hf_architectures_from_repo_local(local_model_dir: str | Path) -> list[str]:
    """Load architectures from locally cached model directory."""
    config_path = Path(local_model_dir) / "config.json"
    if not config_path.exists():
        raise ValueError(f"config.json not found in {local_model_dir}")
    
    with open(config_path) as f:
        config_dict = json.load(f)
    
    architectures = config_dict.get("architectures", None)
    if not architectures:
        raise ValueError("HuggingFace config has no architectures list")
    if isinstance(architectures, str):
        return [architectures]
    if isinstance(architectures, list):
        return [str(item) for item in architectures]

    raise ValueError("HuggingFace architectures must be a string or list")


def get_hf_model_support_level(local_model_dir: str | Path, token: str | None = None) -> int:
    """Classify support level for a locally cached Hugging Face model.

    Args:
        local_model_dir: Path to the locally cached model directory (from snapshot_download)
        token: Unused, kept for compatibility

    Returns:
        0: model architectures in SUPPORTED_HF_MODELS
        1: model architectures in CUSTOM_CONVERTERS
        2: otherwise
    """
    supported_hf_models_lower = {item.lower() for item in SUPPORTED_HF_MODELS}
    custom_converters_lower = {item.lower() for item in CUSTOM_CONVERTERS}

    try:
        architectures = load_hf_architectures_from_repo_local(local_model_dir)
    except ValueError:
        return 2
    except Exception:
        return 2

    normalized_architectures = {architecture.lower() for architecture in architectures}
    if normalized_architectures & supported_hf_models_lower:
        return 0
    if normalized_architectures & custom_converters_lower:
        return 1
    return 2


def get_optimum_export_task(local_model_dir: str | Path) -> str | None:
    """Return explicit optimum export task for local snapshot export.

    Returns None when there is no known task mapping for detected architectures.
    """
    try:
        architectures = load_hf_architectures_from_repo_local(local_model_dir)
    except Exception:
        return None

    for architecture in architectures:
        task = OPTIMUM_TASK_BY_ARCH.get(architecture.lower())
        if task:
            return task
    return None


def custom_conversion(
    local_model_dir: str | Path,
    repo_id: str,
    outdir: Path,
    token: str | None,
    extra_args: list[str] | None = None,
) -> Path:
    """Run custom conversion for architectures listed in CUSTOM_CONVERTERS.
    
    Args:
        local_model_dir: Path to locally cached model from snapshot_download
        repo_id: Original repo ID (for naming output directory)
        outdir: Output directory for conversion
        token: HuggingFace token
        extra_args: Additional arguments for export
    """
    if extra_args is None:
        extra_args = []

    architectures = load_hf_architectures_from_repo_local(local_model_dir)
    primary_arch = architectures[0].lower()

    export_dir = outdir / repo_id.replace("/", "_")
    handlers: dict[str, tuple[str, Callable[[], Path]]] = {
        "clipmodel": (
            "a CLIP model",
            lambda: export_hf_clip_to_openvino(
                local_model_dir,
                export_dir,
                token,
            ),
        ),
        "rtdetrforobjectdetection": (
            "an RT-DETR model",
            lambda: export_hf_rtdetr_to_openvino(
                local_model_dir,
                export_dir,
                token,
                extra_args=extra_args,
            ),
        ),
        "rtdetrv2forobjectdetection": (
            "an RT-DETR v2 model",
            lambda: export_hf_rtdetr_to_openvino(
                local_model_dir,
                export_dir,
                token,
                extra_args=extra_args,
            ),
        ),
        "depthanythingfordepthestimation": (
            "a DepthAnything model",
            lambda: export_hf_depthanything_to_openvino(
                local_model_dir,
                export_dir,
                token,
                extra_args=extra_args,
            ),
        ),
    }

    model_description, export_handler = handlers[primary_arch]
    print(f"Model {repo_id} is {model_description}")
    return export_handler()


def export_hf_clip_to_openvino(
    local_model_dir: str | Path,
    outdir: Path,
    token: str | None,
) -> Path:
    """Export CLIP vision encoder to OpenVINO IR.

    This exports only the visual feature extractor (no text encoder).
    
    Args:
        local_model_dir: Path to locally cached CLIP model
        outdir: Output directory for OpenVINO IR
        token: Unused, kept for compatibility
    """
    outdir.mkdir(parents=True, exist_ok=True)

    # Load from local cached model (already pinned via snapshot_download)
    vision_model = CLIPVisionModel.from_pretrained(str(local_model_dir))  # nosec - model pinned via snapshot_download




    vision_model.eval()

    img = Image.new("RGB", (224, 224))
    processor = AutoProcessor.from_pretrained(str(local_model_dir))




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
    model_name = Path(local_model_dir).name
    save_model(ov_model, str(outdir / f"{model_name}.xml"))

    processor.save_pretrained(str(outdir))
    return outdir


def export_hf_rtdetr_to_openvino(
    local_model_dir: str | Path,
    outdir: Path,
    token: str | None,
    extra_args: list[str] | None = None,
) -> Path:
    """Export RT-DETR via PyTorch -> ONNX -> OpenVINO IR.

    Args:
        local_model_dir: Path to locally cached RT-DETR model
        outdir: Output directory for conversion
        token: HuggingFace token (for private models)
        extra_args: Additional arguments for export

    Requires `optimum`, `huggingface_hub`, and `openvino` to be installed.
    """
    outdir.mkdir(parents=True, exist_ok=True)
    _ = extra_args
    model_onnx = outdir / "model.onnx"

    main_export(
        str(local_model_dir),
        output=outdir,

        task="object-detection",
        opset=18,
        width=640,
        height=640,
        token=token,
    )

    # Copy preprocessor_config.json from local cached model to output directory
    local_model_dir = Path(local_model_dir)
    preprocessor_config_src = local_model_dir / "preprocessor_config.json"
    if preprocessor_config_src.exists():
        shutil.copy(preprocessor_config_src, outdir / "preprocessor_config.json")

    ov_model = convert_model(str(model_onnx))
    model_name = local_model_dir.name
    save_model(ov_model, str(outdir / f"{model_name}.xml"))
    model_onnx.unlink(missing_ok=True)
    return outdir


def export_hf_depthanything_to_openvino(
    local_model_dir: str | Path,
    outdir: Path,
    token: str | None,
    extra_args: list[str] | None = None,
) -> Path:
    """Export DepthAnything via PyTorch -> OpenVINO IR.

    Args:
        local_model_dir: Path to locally cached DepthAnything model
        outdir: Output directory for conversion
        token: Unused, kept for compatibility
        extra_args: Unused, kept for compatibility

    Requires `huggingface_hub` and `openvino` to be installed.
    """
    outdir.mkdir(parents=True, exist_ok=True)
    _ = extra_args
    _ = token
    local_model_dir = Path(local_model_dir)

    model = AutoModelForDepthEstimation.from_pretrained(str(local_model_dir))  # nosec - model pinned via snapshot_download




    model.eval()

    img = Image.new("RGB", (224, 224))
    processor = AutoImageProcessor.from_pretrained(str(local_model_dir))  # nosec - model pinned via snapshot_download




    batch = processor(images=img, return_tensors="pt")["pixel_values"]

    ov_model = convert_model(model, example_input=batch)

    # Copy configs from local cached model to output directory
    for config_file in ["config.json", "preprocessor_config.json"]:
        config_src = local_model_dir / config_file
        config_dst = outdir / config_file
        if config_src.exists():
            shutil.copy(config_src, config_dst)

    model_name = local_model_dir.name
    save_model(ov_model, str(outdir / f"{model_name}.xml"))

    return outdir
