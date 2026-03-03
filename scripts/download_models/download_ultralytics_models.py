# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
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
    if path.exists():
        if path.suffix.lower() != ".pt":
            raise ValueError("Ultralytics local model must be a .pt file")
        return YOLO(str(path))
    return YOLO(model_or_path)


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

        model_path = move_exported_model(Path(exported_model_path), outdir)
        print(f"Exported model location: {model_path}")
    except FileNotFoundError as exc:
        missing = exc.filename or model_name
        print(f"File not found: {missing}")
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
