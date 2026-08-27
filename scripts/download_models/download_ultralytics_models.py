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
from huggingface_hub import hf_hub_download, list_repo_files


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download Ultralytics models and convert them to OpenVINO format."
    )
    parser.add_argument(
        "--model",
        required=True,
        help=(
            "Ultralytics model name, model@revision (GitHub assets release tag), "
            "local path to a .pt file, or Hugging Face repo ID (e.g., 'user/repo')"
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
    parser.add_argument(
        "--classes",
        default=None,
        help="Comma-separated class names to bake into the model via set_classes() (YOLOE text-prompt models only).",
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


def download_pinned_ultralytics_weight(
    model_name: str, revision: str
) -> tuple[Path, Path]:
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
        print(
            f"Downloading pinned Ultralytics weight: {normalized_model_name} @ {revision}"
        )
        if not download_url.startswith("https://"):
            raise ValueError(f"Refusing to download from non-HTTPS URL: {download_url}")
        urlretrieve(
            download_url, local_weight_path
        )  # nosec B310 - URL scheme validated above
    except (HTTPError, URLError) as exc:
        shutil.rmtree(temp_dir, ignore_errors=True)
        raise FileNotFoundError(
            "Unable to download pinned Ultralytics weight "
            f"'{normalized_model_name}' from release '{revision}'"
        ) from exc

    return local_weight_path, temp_dir


def download_from_hf(
    model_id: str, revision: str | None = None
) -> tuple[Path, Path]:
    """
    Downloads the first .pt file found in a Hugging Face Hub repository.

    This function creates a temporary directory for the download, searches the
    specified repository for a file ending in .pt, downloads it, and then
    returns the path to the downloaded file and the temporary directory.

    Args:
        model_id: The ID of the Hugging Face repository (e.g., 'user/repo').

    Returns:
        A tuple containing the Path to the downloaded .pt file and the Path
        to the temporary directory created for the download.

    Raises:
        ImportError: If the 'huggingface-hub' library is not installed.
        FileNotFoundError: If no '.pt' file is found in the repository.
    """
    if hf_hub_download is None or list_repo_files is None:
        raise ImportError(
            "huggingface-hub is not installed. Please run: pip install huggingface-hub"
        )

    print(f"Searching for a .pt file in Hugging Face repo: {model_id}")
    # Create a unique temporary directory for this download
    temp_dir = Path(tempfile.mkdtemp(prefix="hf_model_"))

    try:
        # Get the list of all files in the repository
        repo_files = list_repo_files(repo_id=model_id, revision=revision)
        # Find the first file that ends with .pt
        pt_files = [f for f in repo_files if f.endswith(".pt")]

        if not pt_files:
            raise FileNotFoundError(
                f"No '.pt' model file found in the repo '{model_id}'."
            )

        # If multiple .pt files exist, use the first one and warn the user
        if len(pt_files) > 1:
            print(
                f"Warning: Multiple .pt files found. Using the first one: {pt_files[0]}"
            )

        target_filename = pt_files[0]
        print(f"Found model file: {target_filename}. Downloading...")

        # Download the file to the temporary directory
        local_path = hf_hub_download(
            repo_id=model_id,
            filename=target_filename,
            revision=revision,
            cache_dir=temp_dir,
        )
        return Path(local_path), temp_dir

    except Exception as e:
        # If anything goes wrong, clean up the temporary directory before re-raising
        shutil.rmtree(temp_dir, ignore_errors=True)
        # Re-raise the exception to be handled by the main script logic
        raise e


def resolve_ultralytics_model(model_or_path: str) -> tuple[YOLO, Path | None]:
    model_ref, revision = parse_model_ref(model_or_path)

    # First, check if the model string looks like a Hugging Face repo ID.
    # A simple heuristic is a string containing a '/' that is not an existing local file.
    is_hf_id = (
        "/" in model_ref
        and not Path(model_ref).is_dir()
        and not Path(model_ref).is_file()
    )
    if is_hf_id:
        # If it looks like an HF ID, use the new download function.
        # This returns the local path to the downloaded file and the temp directory.
        local_hf_path, temp_dir = download_from_hf(model_ref, revision)
        return YOLO(str(local_hf_path)), temp_dir

    # If it's not a Hugging Face ID, proceed.
    path = Path(model_or_path)

    # Absolute path or has separators → must be local file
    if path.is_absolute() or ("/" in model_or_path or "\\" in model_or_path):
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

    model_name = model_ref
    if revision:
        pinned_weight_path, temp_dir = download_pinned_ultralytics_weight(
            model_name, revision
        )
        return YOLO(str(pinned_weight_path)), temp_dir

    # Not local → try hub
    return YOLO(normalize_model_filename(model_name)), None


def is_explicit_local_model_path(model_or_path: str) -> bool:
    path = Path(model_or_path)
    return path.is_absolute() or ("/" in model_or_path or "\\" in model_or_path)


def get_output_model_name(model_or_path: str) -> str:
    model_name, _ = parse_model_ref(model_or_path)
    path = Path(model_name)
    if path.is_absolute() or "/" in model_name or "\\" in model_name:
        if "/" in model_name and not path.exists():
            return model_name.replace("/", "_").replace("\\", "_")
        return path.stem
    return path.stem


def move_exported_model(
    exported_path: Path, outdir: Path, model_name: str | None = None
) -> Path:
    for item in exported_path.iterdir():
        target_name = (
            f"{model_name}{item.suffix}"
            if model_name is not None and item.suffix in {".xml", ".bin"}
            else item.name
        )
        target = outdir / target_name
        if target.exists():
            if target.is_dir():
                shutil.rmtree(target)
            else:
                target.unlink()
        # shutil.move handles cross-device moves (e.g. /tmp -> mounted volume).
        shutil.move(str(item), str(target))
    shutil.rmtree(exported_path, ignore_errors=True)
    return outdir


def main() -> int:
    args = parse_args()
    model_name = args.model
    output_model_name = get_output_model_name(model_name)
    outdir = Path(args.outdir)
    half = args.half
    int8 = args.int8
    classes = [c.strip() for c in args.classes.split(",")] if args.classes else None
    temp_download_dir: Path | None = None

    try:
        outdir.mkdir(parents=True, exist_ok=True)
        model, temp_download_dir = resolve_ultralytics_model(model_name)

        if classes is not None:
            if not hasattr(model, "set_classes") or not hasattr(model, "get_text_pe"):
                print(
                    f"Error: --classes is only supported for YOLOE text-prompt models (e.g. yoloe-26s-seg)"
                )
                return 1
            model.set_classes(classes, model.get_text_pe(classes))

        exported_model_path = model.export(
            format="openvino",
            dynamic=True,
            half=half,
            int8=int8,
        )

        if not exported_model_path or not Path(exported_model_path).exists():
            print(f"Error: Export failed for model '{model_name}' - no output produced")
            return 1

        model_path = move_exported_model(
            Path(exported_model_path),
            outdir,
            output_model_name if "/" in model_name and not Path(model_name).exists() else None,
        )
        print(f"Exported model location: {model_path}")
    except FileNotFoundError as exc:
        missing = getattr(exc, "filename", None) or model_name
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
