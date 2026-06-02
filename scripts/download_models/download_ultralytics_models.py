# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Ultralytics model download/export script.
This script allows you to download a model from the Ultralytics hub or load a local .pt file,
and export it to OpenVINO format with optional precision settings.
The exported model will be saved to the specified output directory."""

from __future__ import annotations
import argparse
from pathlib import Path
from ultralytics import YOLO


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download Ultralytics models and convert them to OpenVINO format."
    )
    parser.add_argument(
        "--model",
        required=True,
        help="Ultralytics model name or local path to a .pt file",
    )
    parser.add_argument(
        "--outdir",
        default=".",
        help="Output directory for exported model",
    )
    parser.add_argument(
        "--half",
        action="store_true",
        help="Use FP16 precision for OpenVINO export",
    )
    parser.add_argument(
        "--int8",
        action="store_true",
        help="Use INT8 precision for OpenVINO export",
    )
    return parser.parse_args()


def resolve_ultralytics_model(model_or_path: str) -> YOLO:
    path = Path(model_or_path)

    # Absolute path or has separators → must be local file
    if path.is_absolute() or ('/' in model_or_path or '\\' in model_or_path):
        if not path.exists():
            raise FileNotFoundError(f"Model file not found: {model_or_path}")
        if path.suffix.lower() != ".pt":
            raise ValueError("Ultralytics local model must be a .pt file")
        return YOLO(str(path))

    # Simple name (e.g. "yolo11n.pt") → check local, then try hub
    if path.exists():
        if path.suffix.lower() != ".pt":
            raise ValueError("Ultralytics local model must be a .pt file")
        return YOLO(str(path))

    # Not local → try hub
    return YOLO(model_or_path)


def is_explicit_local_model_path(model_or_path: str) -> bool:
    path = Path(model_or_path)
    return path.is_absolute() or ('/' in model_or_path or '\\' in model_or_path)


def move_exported_model(exported_path: Path, outdir: Path) -> Path:
    desired_path = outdir / exported_path.name
    exported_path.rename(desired_path)
    return desired_path


def main() -> int:
    args = parse_args()
    model_name = args.model
    outdir = Path(args.outdir)
    half = args.half
    int8 = args.int8

    try:
        outdir.mkdir(parents=True, exist_ok=True)
        model = resolve_ultralytics_model(model_name)

        exported_model_path = model.export(
            format="openvino",
            dynamic=True,
            half=half,
            int8=int8,
        )

        if not exported_model_path or not Path(exported_model_path).exists():
            print(f"Error: Export failed for model '{model_name}' - no output produced")
            return 1

        model_path = move_exported_model(Path(exported_model_path), outdir)
        print(f"Exported model location: {model_path}")
    except FileNotFoundError as exc:
        missing = getattr(exc, 'filename', None) or model_name
        if is_explicit_local_model_path(model_name):
            print(f"Local model file not found: {missing}")
        else:
            print(
                f"Unable to resolve Ultralytics model '{model_name}'. "
                "If this is a newer model family, upgrade the 'ultralytics' Python module to a version that "
                "supports it, or provide a local .pt file path."
            )
        return 1
    except ValueError as exc:
        print(str(exc))
        return 1
    except RuntimeError as exc:
        print(str(exc))
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
