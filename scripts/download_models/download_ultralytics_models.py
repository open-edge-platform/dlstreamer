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
import shutil
import tempfile
from urllib.error import HTTPError
from urllib.error import URLError
from urllib.request import urlretrieve
from pathlib import Path
from ultralytics import YOLO


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download Ultralytics models and convert them to OpenVINO format."
    )
    parser.add_argument(
        "--model",
        required=True,
        help=(
            "Ultralytics model name, model@revision (GitHub assets release tag), "
            "or local path to a .pt file"
        ),
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


def parse_model_ref(model_ref: str) -> tuple[str, str | None]:
    """Parse model reference in format 'model@revision' or 'model'."""
    if "@" in model_ref:
        model_name, revision = model_ref.rsplit("@", 1)
        return model_name.strip(), revision.strip()
    return model_ref.strip(), None


def normalize_model_filename(model_name: str) -> str:
    """Ensure model filename has a .pt suffix."""
    return model_name if model_name.endswith(".pt") else f"{model_name}.pt"


def download_pinned_ultralytics_weight(model_name: str, revision: str) -> tuple[Path, Path]:
    """Download a specific model weight from ultralytics/assets release tag.

    Returns:
        Tuple of (downloaded_weight_path, temp_directory_path)
    """
    normalized_model_name = normalize_model_filename(model_name)
    download_url = (
        "https://github.com/ultralytics/assets/releases/download/"
        f"{revision}/{normalized_model_name}"
    )
    temp_dir = Path(tempfile.mkdtemp(prefix="ultralytics_weights_"))
    local_weight_path = temp_dir / normalized_model_name

    try:
        print(f"Downloading pinned Ultralytics weight: {normalized_model_name} @ {revision}")
        urlretrieve(download_url, local_weight_path)
    except (HTTPError, URLError) as exc:
        shutil.rmtree(temp_dir, ignore_errors=True)
        raise FileNotFoundError(
            "Unable to download pinned Ultralytics weight "
            f"'{normalized_model_name}' from release '{revision}'"
        ) from exc

    return local_weight_path, temp_dir


def resolve_ultralytics_model(model_or_path: str) -> tuple[YOLO, Path | None]:
    path = Path(model_or_path)

    # Absolute path or has separators → must be local file
    if path.is_absolute() or ('/' in model_or_path or '\\' in model_or_path):
        if not path.exists():
            raise FileNotFoundError(f"Model file not found: {model_or_path}")
        if path.suffix.lower() != ".pt":
            raise ValueError("Ultralytics local model must be a .pt file")
        return YOLO(str(path)), None

    # Simple name (e.g. "yolo11n.pt") → check local, then try hub
    if path.exists():
        if path.suffix.lower() != ".pt":
            raise ValueError("Ultralytics local model must be a .pt file")
        return YOLO(str(path)), None

    model_name, revision = parse_model_ref(model_or_path)
    if revision:
        pinned_weight_path, temp_dir = download_pinned_ultralytics_weight(model_name, revision)
        return YOLO(str(pinned_weight_path)), temp_dir

    # Not local → try hub
    return YOLO(normalize_model_filename(model_name)), None


def is_explicit_local_model_path(model_or_path: str) -> bool:
    path = Path(model_or_path)
    return path.is_absolute() or ('/' in model_or_path or '\\' in model_or_path)


def move_exported_model(exported_path: Path, outdir: Path) -> Path:
    for item in exported_path.iterdir():
        item.rename(outdir / item.name)
    exported_path.rmdir()
    return outdir


def main() -> int:
    args = parse_args()
    model_name = args.model
    outdir = Path(args.outdir)
    half = args.half
    int8 = args.int8
    temp_download_dir: Path | None = None

    try:
        outdir.mkdir(parents=True, exist_ok=True)
        model, temp_download_dir = resolve_ultralytics_model(model_name)

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
            print(str(exc))
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
    finally:
        if temp_download_dir is not None:
            shutil.rmtree(temp_download_dir, ignore_errors=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
