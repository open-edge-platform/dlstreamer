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
import subprocess
import sys

from huggingface_hub import hf_hub_download
from packaging.version import Version
import torch
import transformers
from openvino import PartialShape
from openvino import Type
from openvino import save_model
from openvino.tools.ovc import convert_model
from transformers import AutoModelForDepthEstimation, CLIPModel, CLIPVisionModel
from transformers import AutoModelForVideoClassification
from transformers import AutoModelForObjectDetection
from transformers import AutoConfig
from transformers import AutoProcessor, AutoImageProcessor
from PIL import Image

SUPPORTED_HF_MODELS = {
    "vitforimageclassification",
    "WhisperForConditionalGeneration",
    "LlavaForConditionalGeneration",
    "LlavaNextForConditionalGeneration",
    "Gemma3ForConditionalGeneration",
    "Qwen2VLForConditionalGeneration",
    "Qwen2_5_VLForConditionalGeneration",
    "Qwen3VLForConditionalGeneration",
    "LlavaQwen2ForCausalLM",
    "BunnyQwenForCausalLM",
    "LlavaNextVideoForConditionalGeneration",
    "MiniCPMO",
    "MiniCPMV",
    "Phi3VForCausalLM",
    "Phi4MMForCausalLM",
    "InternVLChatModel",
}

CUSTOM_CONVERTERS = {
    "clipmodel",
    "rtdetrforobjectdetection",
    "rtdetrv2forobjectdetection",
    "depthanythingfordepthestimation",
    "videomaeforvideoclassification",
}


# Task hints for optimum-cli when exporting from a local snapshot.
# Some models cannot be auto-detected by optimum without explicit --task.
OPTIMUM_TASK_BY_ARCH = {
    "vitforimageclassification": "image-classification",
    "whisperforconditionalgeneration": "automatic-speech-recognition",
    "llavaforconditionalgeneration": "image-text-to-text",
    "llavanextforconditionalgeneration": "image-text-to-text",
    "llavanextvideoforconditionalgeneration": "image-text-to-text",
    "llavaqwen2forcausallm": "image-text-to-text",
    "bunnyqwenforcausallm": "image-text-to-text",
    "minicpmo": "image-text-to-text",
    "minicpmv": "image-text-to-text",
    "phi3vforcausallm": "image-text-to-text",
    "phi4mmforcausallm": "image-text-to-text",
    "qwen2vlforconditionalgeneration": "image-text-to-text",
    "qwen2_5_vlforconditionalgeneration": "image-text-to-text",
    "qwen3vlforconditionalgeneration": "image-text-to-text",
    "internvlchatmodel": "image-text-to-text",
}


# optimum-intel (pinned version) caps the transformers version accepted by these architectures'
# OpenVINO export configs. With the shipped transformers (>= 5.5) the script reports this and the
# user must downgrade transformers (see docs/user-guide/dev_guide/model_conversion_reference.md);
# once downgraded, the same script converts them.
ARCH_MAX_TRANSFORMERS_VERSION = {
    "qwen2vlforconditionalgeneration": "5.0",
    "qwen2_5_vlforconditionalgeneration": "5.0",
    "qwen3vlforconditionalgeneration": "5.0",
    "llavaqwen2forcausallm": "4.53.3",
    "bunnyqwenforcausallm": "4.57.6",
    "llavanextvideoforconditionalgeneration": "4.57.6",
    "minicpmo": "4.51.3",
    "minicpmv": "4.57.6",
    "phi3vforcausallm": "4.53.3",
    "phi4mmforcausallm": "4.53.3",
    "internvlchatmodel": "4.57.6",
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


def get_hf_model_support_level(local_model_dir: str | Path) -> int:
    """Classify support level for a locally cached Hugging Face model.

    Args:
        local_model_dir: Path to the locally cached model directory

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


def requires_transformers_downgrade(local_model_dir: str | Path) -> str | None:
    """Return the max transformers version needed to convert this model when the installed
    transformers is too new for its optimum-intel export config; otherwise None."""
    try:
        architectures = load_hf_architectures_from_repo_local(local_model_dir)
    except Exception:
        return None
    installed = Version(transformers.__version__)
    for arch in architectures:
        cap = ARCH_MAX_TRANSFORMERS_VERSION.get(arch.lower())
        if cap and installed > Version(cap):
            return cap
    return None


def requires_trust_remote_code(local_model_dir: str | Path) -> bool:
    """Return True if model config indicates custom remote code is required."""
    config_path = Path(local_model_dir) / "config.json"
    if not config_path.exists():
        return False

    try:
        with open(config_path) as f:
            config_dict = json.load(f)
    except Exception:
        return False

    auto_map = config_dict.get("auto_map")
    if not auto_map:
        return False

    if isinstance(auto_map, dict):
        return bool(auto_map)
    return bool(auto_map)


def install_model_requirements(local_model_dir: str | Path) -> None:
    """Install model requirements if requirements.txt exists in the model directory.

    Args:
        local_model_dir: Path to the locally cached model directory
    """
    requirements_file = Path(local_model_dir) / "requirements.txt"
    if not requirements_file.exists():
        return

    print(f"Installing model requirements from {requirements_file}")
    try:
        subprocess.run(
            [sys.executable, "-m", "pip", "install", "-r", str(requirements_file)],
            check=True,
        )
        print("Model requirements installed successfully")
    except subprocess.CalledProcessError as e:
        print(f"Warning: Failed to install model requirements: {str(e)}")
        raise


def custom_conversion(
    local_model_dir: str | Path,
    repo_id: str,
    outdir: Path,
    token: str | None,
    extra_args: list[str] | None = None,
) -> Path:
    """Run custom conversion for architectures listed in CUSTOM_CONVERTERS.

    Args:
        local_model_dir: Path to locally cached model
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
    zeroshot_requested = "--zeroshot" in extra_args or "zeroshot" in extra_args
    handlers: dict[str, tuple[str, Callable[[], Path]]] = {
        "clipmodel": (
            "a CLIP model",
            lambda: (
                export_hf_clip_zeroshot_to_openvino(
                    local_model_dir,
                    export_dir,
                    token,
                )
                if zeroshot_requested
                else export_hf_clip_to_openvino(
                    local_model_dir,
                    export_dir,
                    token,
                )
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
        "videomaeforvideoclassification": (
            "a VideoMAE model",
            lambda: export_hf_videomae_to_openvino(
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

    # Load from the local cached model directory.
    vision_model = CLIPVisionModel.from_pretrained(
        str(local_model_dir)
    )  # nosec - model pinned via snapshot_download

    vision_model.eval()

    img = Image.new("RGB", (224, 224))
    processor = AutoProcessor.from_pretrained(
        str(local_model_dir), local_files_only=True
    )  # nosec B615 - model pinned via snapshot_download

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


def export_hf_clip_zeroshot_to_openvino(
    local_model_dir: str | Path,
    outdir: Path,
    token: str | None,
) -> Path:
    """Export the CLIP image encoder with the visual projection, for zero-shot classification.

    ``export_hf_clip_to_openvino`` exports the unprojected vision tower (the ``clip_token`` path).
    Zero-shot classification instead needs the projected image embedding, the vector that lives in
    CLIP's shared image/text space, so it can be compared against text-label embeddings by cosine
    similarity. This exports ``CLIPModel.get_image_features`` to produce that projected output.

    Preprocessing (CLIP mean/std, RGB, center crop) is written into the model_info section of
    model.xml, so no DL Streamer model-proc file is required.

    Args:
        local_model_dir: Path to locally cached CLIP model
        outdir: Output directory for OpenVINO IR
        token: Unused, kept for compatibility
    """
    outdir.mkdir(parents=True, exist_ok=True)

    # Load from the local cached model directory.
    clip_model = CLIPModel.from_pretrained(
        str(local_model_dir)
    )  # nosec - model pinned via snapshot_download

    clip_model.eval()

    class _CLIPImageEmbedder(torch.nn.Module):
        """Wrap CLIPModel so the traced graph outputs only the projected image embedding."""

        def __init__(self, model: CLIPModel) -> None:
            super().__init__()
            self.model = model

        def forward(self, pixel_values: torch.Tensor) -> torch.Tensor:
            return self.model.get_image_features(pixel_values=pixel_values)

    embedder = _CLIPImageEmbedder(clip_model)
    embedder.eval()

    img = Image.new("RGB", (224, 224))
    processor = AutoProcessor.from_pretrained(
        str(local_model_dir), local_files_only=True
    )  # nosec B615 - model pinned via snapshot_download

    batch = processor.image_processor(images=img, return_tensors="pt")["pixel_values"]

    with torch.no_grad():
        ov_model = convert_model(embedder, example_input=batch)

    # Dynamic batch, static spatial dims, float input.
    input_shape = PartialShape([-1, batch.shape[1], batch.shape[2], batch.shape[3]])
    for nn_input in ov_model.inputs:
        nn_input.get_node().set_partial_shape(input_shape)
        nn_input.get_node().set_element_type(Type.f32)

    # model_type selects the post-processing converter in the pipeline. Zero-shot models are tagged
    # clip_zeroshot so they can never be confused with the unprojected clip_token export above.
    ov_model.set_rt_info("clip_zeroshot", ["model_info", "model_type"])
    # CLIP preprocessing carried in model_info; scale/mean are the CLIP std/mean x 255.
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
    """Export RT-DETR via PyTorch -> OpenVINO IR.

    Args:
        local_model_dir: Path to locally cached RT-DETR model
        outdir: Output directory for conversion
        token: Unused, kept for compatibility
        extra_args: Unused, kept for compatibility

    Requires `transformers`, `huggingface_hub`, and `openvino` to be installed.
    """
    outdir.mkdir(parents=True, exist_ok=True)
    _ = extra_args
    _ = token
    local_model_dir = Path(local_model_dir)

    model = AutoModelForObjectDetection.from_pretrained(
        str(local_model_dir), local_files_only=True
    )  # nosec B615 - model pinned via snapshot_download
    model.eval()

    processor = AutoImageProcessor.from_pretrained(
        str(local_model_dir), local_files_only=True
    )  # nosec B615 - model pinned via snapshot_download

    img = Image.new("RGB", (640, 640))
    batch = processor(images=img, return_tensors="pt")["pixel_values"]

    # RT-DETR's output object mixes Tensor and List[Tensor], which the tracer rejects;
    # expose only the detection tensors so convert_model can trace it.
    class _RTDetrDetectionHead(torch.nn.Module):
        def __init__(self, detector: torch.nn.Module) -> None:
            super().__init__()
            self.detector = detector

        def forward(self, pixel_values: torch.Tensor):
            outputs = self.detector(pixel_values=pixel_values)
            return outputs.logits, outputs.pred_boxes

    traceable_model = _RTDetrDetectionHead(model)
    traceable_model.eval()

    ov_model = convert_model(traceable_model, example_input=batch)

    # Fix spatial dims at 640x640, allow dynamic batch.
    input_shape = PartialShape([-1, batch.shape[1], batch.shape[2], batch.shape[3]])
    for nn_input in ov_model.inputs:
        nn_input.get_node().set_partial_shape(input_shape)
        nn_input.get_node().set_element_type(Type.f32)

    # Traced tuple outputs are unnamed; name them so the backend exposes two distinct blobs
    # (the RT-DETR converter looks up logits and boxes by name). Order matches the wrapper.
    ov_model.outputs[0].get_tensor().set_names({"logits"})
    ov_model.outputs[1].get_tensor().set_names({"pred_boxes"})

    # DL Streamer needs config.json (architecture/labels) next to the IR, plus preprocessor_config.json.
    for config_file in ("config.json", "preprocessor_config.json"):
        config_src = local_model_dir / config_file
        if config_src.exists():
            shutil.copy(config_src, outdir / config_file)

    model_name = local_model_dir.name
    save_model(ov_model, str(outdir / f"{model_name}.xml"))
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

    model = AutoModelForDepthEstimation.from_pretrained(
        str(local_model_dir), local_files_only=True
    )  # nosec B615 - model pinned via snapshot_download

    model.eval()

    img = Image.new("RGB", (224, 224))
    processor = AutoImageProcessor.from_pretrained(
        str(local_model_dir), local_files_only=True
    )  # nosec B615 - model pinned via snapshot_download

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


def export_hf_videomae_to_openvino(
    local_model_dir: str | Path,
    outdir: Path,
    token: str | None,
    extra_args: list[str] | None = None,
) -> Path:
    """Export VideoMAE via PyTorch -> OpenVINO IR.

    Args:
        local_model_dir: Path to locally cached VideoMAE model
        outdir: Output directory for conversion
        token: Unused, kept for compatibility
        extra_args: Unused, kept for compatibility

    Requires `transformers`, `huggingface_hub`, and `openvino` to be installed.
    """
    outdir.mkdir(parents=True, exist_ok=True)
    _ = extra_args
    _ = token
    local_model_dir = Path(local_model_dir)

    model = AutoModelForVideoClassification.from_pretrained(
        str(local_model_dir), local_files_only=True
    )  # nosec B615 - model pinned via snapshot_download
    model.eval()

    processor = AutoImageProcessor.from_pretrained(
        str(local_model_dir), local_files_only=True
    )  # nosec B615 - model pinned via snapshot_download

    config = AutoConfig.from_pretrained(
        str(local_model_dir), local_files_only=True
    )  # nosec B615 - model pinned via snapshot_download
    num_frames = int(getattr(config, "num_frames", 16))
    image_size = int(getattr(config, "image_size", 224))

    frames = [Image.new("RGB", (image_size, image_size)) for _ in range(num_frames)]
    batch = processor(images=frames, return_tensors="pt")["pixel_values"]
    if batch.dim() == 4:
        batch = batch.unsqueeze(0)

    ov_model = convert_model(model, example_input=batch)

    for config_file in ["config.json", "preprocessor_config.json"]:
        config_src = local_model_dir / config_file
        config_dst = outdir / config_file
        if config_src.exists():
            shutil.copy(config_src, config_dst)

    model_name = local_model_dir.name
    save_model(ov_model, str(outdir / f"{model_name}.xml"))

    return outdir
