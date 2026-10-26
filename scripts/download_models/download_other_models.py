#!/usr/bin/env python3
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Download and convert models that are not covered by the Ultralytics/HF/TIMM scripts.
"""

from __future__ import annotations

import argparse
import contextlib
import functools
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import traceback
import urllib.error
import urllib.request
import zipfile
from pathlib import Path
from typing import TYPE_CHECKING, Callable, Iterator

if TYPE_CHECKING:
    import openvino as ov


SCRIPT_DIR = Path(__file__).resolve().parent
MAX_OMZ_PYTHON_VERSION = (3, 12)
USER_AGENT = "dlstreamer-download-models"
TORCH_CPU_INDEX = "https://download.pytorch.org/whl/cpu"
TORCH_PACKAGES = ("torch==2.13.0", "torchaudio==2.11.0", "torchvision==0.28.0")
OMZ_TOOLS_PACKAGE = "openvino-dev==2024.6.0"
LICENSE_PLATE_READER_ZIP = (
    "https://github.com/open-edge-platform/edge-ai-resources/raw/"
    "948b6efb686df9668b633a11c40b006db1fc1e1d/models/license-plate-reader.zip"
)


# ================================= Helpers =================================


def log(message: str) -> None:
    print(message, flush=True)


def display_header(text: str) -> None:
    log("")
    log("=" * 63)
    log(f"  {text}")
    log("=" * 63)
    log("")


def report_cached(path: Path) -> None:
    log(f"\nModel already exists: {path}.\n")


def all_exist(*paths: Path) -> bool:
    return all(path.is_file() for path in paths)


def run(
    command: list,
    cwd: Path | None = None,
    env: dict | None = None,
    retries: int = 1,
    retry_delay: int = 0,
) -> None:
    command = [str(part) for part in command]
    for attempt in range(1, retries + 1):
        log(f"[RUN] {' '.join(command)}" + (f"  (cwd: {cwd})" if cwd else ""))
        try:
            subprocess.run(command, cwd=cwd, env=env, check=True)
            return
        except subprocess.CalledProcessError:
            if attempt == retries:
                raise
            log(f"[WARN] Attempt {attempt}/{retries} failed, retrying in {retry_delay}s...")
            time.sleep(retry_delay)


def venv_executable(venv_dir: Path, name: str) -> Path:
    """Return the path of an executable inside a virtual environment."""
    if os.name == "nt":
        return venv_dir / "Scripts" / f"{name}.exe"
    return venv_dir / "bin" / name


def _make_writable_and_retry(func, path, _exc) -> None:
    # Git objects and files extracted from archives can be read-only, which makes
    # rmtree fail on Windows (equivalent of `chmod -R u+w` in the shell script).
    os.chmod(path, stat.S_IWRITE)
    func(path)


def remove_path(path: Path) -> None:
    """Remove a file or a directory tree, tolerating read-only entries."""
    if path.is_dir() and not path.is_symlink():
        if sys.version_info >= (3, 12):
            shutil.rmtree(path, onexc=_make_writable_and_retry)
        else:
            shutil.rmtree(path, onerror=_make_writable_and_retry)
    elif path.exists() or path.is_symlink():
        os.chmod(path, stat.S_IWRITE)
        path.unlink()


@contextlib.contextmanager
def staging_dir(parent: Path) -> Iterator[Path]:
    """Temporary working directory inside `parent`, removed on exit."""
    parent.mkdir(parents=True, exist_ok=True)
    path = Path(tempfile.mkdtemp(prefix=".staging-", dir=parent))
    try:
        yield path
    finally:
        remove_path(path)


def _looks_like_error_payload(path: Path, extension: str) -> bool:
    """Detect HTML/JSON/text error pages saved in place of the expected file."""
    with path.open("rb") as file:
        head = file.read(4096)
    stripped = head.lstrip().lower()
    is_html = stripped.startswith((b"<!doctype html", b"<html"))
    is_json = stripped.startswith((b"{", b"[")) and b"\x00" not in head
    if extension == "xml":
        # OpenVINO IR files (.xml) are valid text/xml; reject only obvious error payloads.
        return is_html or is_json
    # For non-XML artifacts (bin/zip/onnx/etc.), a text payload usually means an error page.
    if b"\x00" in head:
        return False
    try:
        head.decode("utf-8")
    except UnicodeDecodeError:
        return False
    return True


def download_file(url: str, destination: Path, retries: int = 3) -> None:
    """Download `url` to `destination` with retries and basic payload validation."""
    if not url.startswith("https://"):
        raise ValueError(f"Refusing to download from non-HTTPS URL: {url}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    last_error: Exception | None = None
    for attempt in range(1, retries + 1):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(request, timeout=120) as response, destination.open("wb") as output:  # nosec B310 - URL scheme validated above
                shutil.copyfileobj(response, output)
            break
        except (urllib.error.URLError, OSError) as exc:
            last_error = exc
            log(f"[WARN] Download attempt {attempt}/{retries} failed for {url}: {exc}")
            time.sleep(2 * attempt)
    else:
        raise RuntimeError(f"Failed to download {url}") from last_error

    extension = destination.suffix.lstrip(".").lower()
    if _looks_like_error_payload(destination, extension):
        with destination.open("rb") as file:
            preview = file.read(512).decode("utf-8", errors="replace")
        destination.unlink()
        raise RuntimeError(f"Unexpected response while downloading {url}:\n{preview}")


def extract_zip(archive: Path, destination: Path) -> None:
    with zipfile.ZipFile(archive, "r") as zip_ref:
        zip_ref.extractall(destination)
    archive.unlink()


def git_clone(url: str, destination: Path) -> None:
    remove_path(destination)
    run([
        "git", "-c", "core.autocrlf=false", "-c", "core.longpaths=true",
        "clone", "--depth", "1", url, destination,
    ])


def patch_file(path: Path, old: str, new: str) -> None:
    """In-place string replacement (equivalent of `sed -i s/old/new/g`)."""
    content = path.read_text(encoding="utf-8")
    if old not in content:
        log(f"[WARN] Patch pattern not found in {path}: {old}")
        return
    path.write_text(content.replace(old, new), encoding="utf-8")


def read_model(xml_path: Path) -> ov.Model:
    """Read an IR fully into memory so its files can be deleted afterwards (needed on Windows)."""
    import openvino as ov  # pylint: disable=import-outside-toplevel,redefined-outer-name

    core = ov.Core()
    core.set_property({"ENABLE_MMAP": False})
    return core.read_model(model=str(xml_path))


def onnx_to_ir(
    onnx_path: Path,
    outputs: list[tuple[Path, bool]],
    input_shape: list[int] | None = None,
) -> None:
    """Convert an ONNX model once and save it to each (xml_path, compress_to_fp16) target.

    Equivalent of `ovc <onnx> [--input <shape>] --output_model <xml> --compress_to_fp16=<bool>`.
    """
    import openvino as ov  # pylint: disable=import-outside-toplevel,redefined-outer-name

    kwargs = {"input": ov.PartialShape(input_shape)} if input_shape else {}
    ov_model = ov.convert_model(str(onnx_path), **kwargs)
    for xml_path, compress_to_fp16 in outputs:
        xml_path.parent.mkdir(parents=True, exist_ok=True)
        ov.save_model(ov_model, str(xml_path), compress_to_fp16=compress_to_fp16)
    del ov_model


@contextlib.contextmanager
def temporary_venv(package_groups: list[list[str]]) -> Iterator[tuple[Path, dict]]:
    """Create a throw-away venv (e.g. for openvino-dev omz tools) and yield (venv_dir, env)."""
    venv_dir = Path(tempfile.mkdtemp(prefix="dlstreamer_openvino_dev_"))
    try:
        run([sys.executable, "-m", "venv", venv_dir])
        python = venv_executable(venv_dir, "python")
        for packages in package_groups:
            run([python, "-m", "pip", "install", "--no-cache-dir", *packages])
        env = os.environ.copy()
        env.pop("PYTHONHOME", None)
        env["VIRTUAL_ENV"] = str(venv_dir)
        env["PATH"] = str(python.parent) + os.pathsep + env.get("PATH", "")
        yield venv_dir, env
    finally:
        remove_path(venv_dir)


class ModelSkipped(Exception):
    """Raised when a model cannot be converted with the current Python version."""


def run_omz_tools(models_path: Path, model_name: str, package_groups: list[list[str]]) -> None:
    """Run omz_downloader + omz_converter from a temporary openvino-dev venv."""
    if sys.version_info[:2] > MAX_OMZ_PYTHON_VERSION:
        raise ModelSkipped(
            f"requires Python <= {MAX_OMZ_PYTHON_VERSION[0]}.{MAX_OMZ_PYTHON_VERSION[1]} (openvino-dev 2024.6.0), "
            f"current Python is {sys.version_info.major}.{sys.version_info.minor}"
        )
    with temporary_venv(package_groups) as (venv_dir, env):
        run([venv_executable(venv_dir, "omz_downloader"), "--name", model_name], cwd=models_path, env=env)
        run([venv_executable(venv_dir, "omz_converter"), "--name", model_name], cwd=models_path, env=env)


# ================================= Models =================================


def download_yolox_tiny(models_path: Path) -> None:
    model_name = "yolox-tiny"
    model_dir = models_path / "public" / model_name
    if all_exist(model_dir / "FP16" / f"{model_name}.xml", model_dir / "FP32" / f"{model_name}.xml"):
        report_cached(model_dir)
        return

    log(f"Downloading and converting: {model_dir}")
    run_omz_tools(models_path, model_name, [
        [OMZ_TOOLS_PACKAGE],
        ["--extra-index-url", TORCH_CPU_INDEX, *TORCH_PACKAGES],
        ["onnxscript==0.7.1"],
    ])

    # Clean up temporary files created by omz_converter
    for entry in model_dir.iterdir():
        if entry.name.startswith("yolox") or entry.name in ("models", "utils"):
            remove_path(entry)


def download_yolox_s(models_path: Path) -> None:
    model_name = "yolox_s"
    model_dir = models_path / "public" / model_name
    fp16_xml = model_dir / "FP16" / f"{model_name}.xml"
    fp32_xml = model_dir / "FP32" / f"{model_name}.xml"
    if all_exist(fp16_xml, fp32_xml):
        report_cached(model_dir)
        return

    onnx_path = model_dir / "yolox_s.onnx"
    download_file(
        "https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_s.onnx",
        onnx_path,
    )
    onnx_to_ir(onnx_path, [(fp16_xml, True), (fp32_xml, False)])
    onnx_path.unlink()


def download_yolov7(models_path: Path) -> None:
    model_name = "yolov7"
    model_dir = models_path / "public" / model_name
    fp16_xml = model_dir / "FP16" / f"{model_name}.xml"
    fp32_xml = model_dir / "FP32" / f"{model_name}.xml"
    if all_exist(fp16_xml, fp32_xml):
        report_cached(model_dir)
        return

    log(f"Downloading and converting: {model_dir}")
    repo = model_dir / "yolov7"
    git_clone("https://github.com/WongKinYiu/yolov7.git", repo)

    # Patch for PyTorch 2.6+ compatibility (weights_only parameter)
    patch_file(
        repo / "models" / "experimental.py",
        "torch.load(w, map_location=map_location)",
        "torch.load(w, map_location=map_location, weights_only=False)",
    )
    # Use the legacy ONNX exporter because the YOLOv7 graph is incompatible with Dynamo.
    patch_file(repo / "export.py", "opset_version=12, input_names=", "opset_version=12, dynamo=False, input_names=")

    # yolov7's attempt_download() queries the GitHub "latest release" API (returns 404 for this repo)
    # and then falls back to `git tag`, which is empty in a shallow clone. Fetch the weights directly.
    download_file("https://github.com/WongKinYiu/yolov7/releases/download/v0.1/yolov7.pt", repo / "yolov7.pt")

    run([sys.executable, "export.py", "--weights", "yolov7.pt", "--grid", "--dynamic-batch"], cwd=repo)
    onnx_to_ir(repo / "yolov7.onnx", [(fp16_xml, True), (fp32_xml, False)])
    remove_path(repo)


def download_license_plate_reader_model(
    models_path: Path, model_name: str, source_subdir: str, source_stem: str
) -> None:
    """Extract one FP32 model from the Edge AI Resources license-plate-reader bundle."""
    model_dir = models_path / "public" / model_name
    fp32_dir = model_dir / "FP32"
    if all_exist(fp32_dir / f"{model_name}.xml"):
        report_cached(model_dir)
        return

    log(f"Downloading and converting: {model_dir}")
    with staging_dir(model_dir) as tmp:
        archive = tmp / f"{model_name}.zip"
        download_file(LICENSE_PLATE_READER_ZIP, archive)
        extract_zip(archive, tmp)

        source_dir = tmp / "license-plate-reader" / "models" / source_subdir
        fp32_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source_dir / f"{source_stem}.bin", fp32_dir / f"{model_name}.bin")
        shutil.copyfile(source_dir / f"{source_stem}.xml", fp32_dir / f"{model_name}.xml")


def download_centerface(models_path: Path) -> None:
    import openvino as ov  # pylint: disable=import-outside-toplevel,redefined-outer-name

    model_name = "centerface"
    model_dir = models_path / "public" / model_name
    if all_exist(model_dir / "FP16" / f"{model_name}.xml", model_dir / "FP32" / f"{model_name}.xml"):
        report_cached(model_dir)
        return

    log(f"Downloading and converting: {model_dir}")
    with staging_dir(model_dir) as tmp:
        repo = tmp / "CenterFace"
        git_clone("https://github.com/Star-Clouds/CenterFace.git", repo)

        # Intermediate IR keeps the `ovc` CLI default (FP16-compressed weights), as in the shell script.
        intermediate_xml = tmp / f"{model_name}.xml"
        onnx_to_ir(repo / "models" / "onnx" / "centerface.onnx", [(intermediate_xml, True)], [1, 3, 768, 1280])

        ov_model = read_model(intermediate_xml)
        ov_model.output(0).set_names({"heatmap"})
        ov_model.output(1).set_names({"scale"})
        ov_model.output(2).set_names({"offset"})
        ov_model.output(3).set_names({"landmarks"})

        ov_model.set_rt_info("centerface", ["model_info", "model_type"])
        ov_model.set_rt_info("0.55", ["model_info", "confidence_threshold"])
        ov_model.set_rt_info("0.5", ["model_info", "iou_threshold"])

        ov.save_model(ov_model, str(model_dir / "FP32" / f"{model_name}.xml"), compress_to_fp16=False)
        ov.save_model(ov_model, str(model_dir / "FP16" / f"{model_name}.xml"), compress_to_fp16=True)
        del ov_model


def download_hsemotion(models_path: Path) -> None:
    import openvino as ov  # pylint: disable=import-outside-toplevel,redefined-outer-name

    model_name = "hsemotion"
    model_dir = models_path / "public" / model_name
    if all_exist(model_dir / "FP16" / f"{model_name}.xml"):
        report_cached(model_dir)
        return

    log(f"Downloading and converting: {model_dir}")
    with staging_dir(model_dir) as tmp:
        repo = tmp / "face-emotion-recognition"
        git_clone("https://github.com/av-savchenko/face-emotion-recognition.git", repo)

        intermediate_xml = tmp / f"{model_name}.xml"
        onnx_path = repo / "models" / "affectnet_emotions" / "onnx" / "enet_b0_8_va_mtl.onnx"
        onnx_to_ir(onnx_path, [(intermediate_xml, True)], [16, 3, 224, 224])

        ov_model = read_model(intermediate_xml)
        ov_model.set_rt_info(
            "anger contempt disgust fear happiness neutral sadness surprise", ["model_info", "labels"]
        )
        ov_model.set_rt_info("label", ["model_info", "model_type"])
        ov_model.set_rt_info("True", ["model_info", "output_raw_scores"])
        ov_model.set_rt_info("fit_to_window_letterbox", ["model_info", "resize_type"])
        ov_model.set_rt_info("255", ["model_info", "scale_values"])

        ov.save_model(ov_model, str(model_dir / "FP16" / f"{model_name}.xml"))
        del ov_model


def download_deeplabv3(models_path: Path) -> None:
    import openvino as ov  # pylint: disable=import-outside-toplevel,redefined-outer-name

    model_name = "deeplabv3"
    model_dir = models_path / "public" / model_name
    fp32_xml = model_dir / "FP32" / f"{model_name}.xml"
    fp16_xml = model_dir / "FP16" / f"{model_name}.xml"
    if all_exist(fp32_xml, fp16_xml):
        report_cached(model_dir)
        return

    log(f"Downloading and converting: {model_dir}")
    run_omz_tools(models_path, model_name, [[OMZ_TOOLS_PACKAGE], ["tensorflow==2.20.0"]])

    ov_model = read_model(fp32_xml)
    ov_model.set_rt_info("semantic_mask", ["model_info", "model_type"])

    remove_path(model_dir / "deeplabv3_mnv2_pascal_train_aug")
    remove_path(model_dir / "FP32")
    remove_path(model_dir / "FP16")
    ov.save_model(ov_model, str(fp32_xml), compress_to_fp16=False)
    ov.save_model(ov_model, str(fp16_xml), compress_to_fp16=True)
    del ov_model


def download_aclnet(models_path: Path) -> None:
    model_name = "aclnet"
    model_dir = models_path / "public" / model_name
    fp16_xml = model_dir / "FP16" / f"{model_name}.xml"
    fp32_xml = model_dir / "FP32" / f"{model_name}.xml"
    if all_exist(fp16_xml, fp32_xml):
        report_cached(model_dir)
        return

    log(f"Downloading and converting: {model_dir}")
    onnx_path = model_dir / "aclnet_des_53.onnx"
    download_file(
        "https://storage.openvinotoolkit.org/models_contrib/sound_classification/aclnet/2021-02-04/aclnet_des_53_fp32.onnx",
        onnx_path,
    )
    onnx_to_ir(onnx_path, [(fp16_xml, True), (fp32_xml, False)], [1, 1, 1, 16000])
    onnx_path.unlink()


def download_ppocr_v5(models_path: Path, model_name: str) -> None:
    """PP-OCRv5 PaddlePaddle models FP32 & FP16 - HuggingFace + paddle2onnx."""
    model_dir = models_path / "public" / model_name
    fp32_dir = model_dir / "FP32"
    fp16_dir = model_dir / "FP16"
    if all_exist(fp32_dir / f"{model_name}.xml", fp16_dir / f"{model_name}.xml"):
        report_cached(model_dir)
        return

    log(f"Downloading and converting: {model_dir}")
    # Dependencies required for PaddlePaddle PIR -> ONNX conversion. They live in a temporary venv
    # because paddle2onnx pins onnx<=1.17 and would otherwise downgrade onnx in the main environment.
    # On Windows, paddle2onnx 2.1.0 fails to load its native extension with paddlepaddle >= 3.2.0
    # ("DLL load failed ... The specified procedure could not be found"), so paddle is pinned there.
    # paddle imports setuptools at runtime without declaring it (fresh Python 3.12 venvs lack it).
    paddle_package = "paddlepaddle==3.1.1" if os.name == "nt" else "paddlepaddle"
    paddle_packages = [[paddle_package, "setuptools", "paddle2onnx", "huggingface_hub"]]
    with temporary_venv(paddle_packages) as (venv_dir, env), staging_dir(model_dir) as tmp:
        paddle_python = venv_executable(venv_dir, "python")
        paddle_dir = tmp / "paddle_model"
        log(f"[1/4] Downloading PaddlePaddle/{model_name} from HuggingFace...")
        # Unauthenticated HF API calls are rate limited per IP (HTTP 429, "retry after ~2 min");
        # set HF_TOKEN to avoid it. Retry to also ride out transient proxy errors.
        run([
            paddle_python, "-c",
            "import sys; from huggingface_hub import snapshot_download; "
            "snapshot_download(repo_id=sys.argv[1], local_dir=sys.argv[2])",
            f"PaddlePaddle/{model_name}", paddle_dir,
        ], env=env, retries=3, retry_delay=150)

        log("[2/4] Converting PaddlePaddle -> ONNX...")
        onnx_path = tmp / "model.onnx"
        run([
            venv_executable(venv_dir, "paddle2onnx"),
            "--model_dir", paddle_dir,
            "--model_filename", "inference.json",
            "--params_filename", "inference.pdiparams",
            "--save_file", onnx_path,
            "--opset_version", "14",
        ], env=env)

        log("[3/4] Converting ONNX -> OpenVINO IR (FP32 & FP16)...")
        onnx_to_ir(onnx_path, [(fp32_dir / f"{model_name}.xml", False), (fp16_dir / f"{model_name}.xml", True)])

        log("[4/4] Storing model config.json...")
        shutil.copyfile(paddle_dir / "config.json", fp32_dir / "config.json")
        shutil.copyfile(paddle_dir / "config.json", fp16_dir / "config.json")


def download_mars_small128(models_path: Path) -> None:
    model_name = "mars-small128"
    model_dir = models_path / "public" / model_name
    targets = {"FP32": "mars_small128_fp32", "INT8": "mars_small128_int8"}
    if all_exist(*(model_dir / precision / f"{stem}.xml" for precision, stem in targets.items())):
        report_cached(model_dir)
        return

    log(f"Downloading and converting: {model_dir}")
    model_dir.mkdir(parents=True, exist_ok=True)
    converter = SCRIPT_DIR / "convert_mars_deepsort.py"
    run([sys.executable, converter, "--output-dir", model_dir, "--precision", "both"], retries=3, retry_delay=30)

    for precision, stem in targets.items():
        (model_dir / precision).mkdir(parents=True, exist_ok=True)
        for extension in (".xml", ".bin"):
            os.replace(model_dir / f"{stem}{extension}", model_dir / precision / f"{stem}{extension}")


def download_pallet_defect_detection(models_path: Path) -> None:
    model_name = "pallet_defect_detection"
    model_dir = models_path / "public" / model_name
    int8_dir = model_dir / "INT8"
    if all_exist(int8_dir / f"{model_name}.xml"):
        report_cached(model_dir)
        return

    log(f"Downloading and converting: {model_dir}")
    with staging_dir(model_dir) as tmp:
        archive = tmp / f"{model_name}.zip"
        download_file(
            "https://github.com/open-edge-platform/edge-ai-resources/raw/"
            "06bb0d621cb14a1791672552a538beddddcc4066/models/INT8/pallet_defect_detection.zip",
            archive,
        )
        extract_zip(archive, tmp)

        source_dir = tmp / "deployment" / "Detection" / "model"
        int8_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source_dir / "model.bin", int8_dir / f"{model_name}.bin")
        shutil.copyfile(source_dir / "model.xml", int8_dir / f"{model_name}.xml")
        shutil.copyfile(source_dir / "config.json", int8_dir / "config.json")


def download_colorcls2(models_path: Path) -> None:
    model_name = "colorcls2"
    model_dir = models_path / "public" / model_name / "FP32"
    if all_exist(model_dir / f"{model_name}.xml"):
        report_cached(model_dir / f"{model_name}.xml")
        return

    log(f"Downloading: {model_dir}")
    base_url = (
        "https://github.com/open-edge-platform/metro-ai-suite/raw/41485929ec29c05c4f37f3d840117aed9f98b85c/"
        "metro-vision-ai-app-recipe/smart-parking/src/dlstreamer-pipeline-server/models/colorcls2"
    )
    for file_name in ("colorcls2.bin", "colorcls2.xml"):
        download_file(f"{base_url}/{file_name}", model_dir / file_name)


def download_pointpillars(models_path: Path) -> None:
    model_dir = models_path / "public" / "pointpillars" / "FP16"
    base_url = (
        "https://raw.githubusercontent.com/openvinotoolkit/openvino_contrib/master/"
        "modules/3d/pointPillars/pretrained"
    )
    file_names = (
        "pointpillars_ov_nn.bin",
        "pointpillars_ov_nn.xml",
        "pointpillars_ov_pillar_layer.xml",
        "pointpillars_ov_postproc.xml",
    )
    if all_exist(*(model_dir / name for name in file_names)):
        report_cached(model_dir)
        return

    log(f"Downloading: {model_dir}")
    for file_name in file_names:
        download_file(f"{base_url}/{file_name}", model_dir / file_name)


MODEL_HANDLERS: dict[str, tuple[str, Callable[[Path], None]]] = {
    "yolox-tiny": ("YOLOx-TINY", download_yolox_tiny),
    "yolox_s": ("YOLOx-S", download_yolox_s),
    "yolov7": ("YOLOv7", download_yolov7),
    "yolov8_license_plate_detector": (
        "YOLOv8 License Plate Detector",
        functools.partial(
            download_license_plate_reader_model,
            model_name="yolov8_license_plate_detector",
            source_subdir="yolov8n",
            source_stem="yolov8n_retrained",
        ),
    ),
    "centerface": ("CenterFace", download_centerface),
    "hsemotion": ("HSEmotion", download_hsemotion),
    "deeplabv3": ("DeepLabv3", download_deeplabv3),
    "aclnet": ("AclNet", download_aclnet),
    "ch_PP-OCRv4_rec_infer": (
        "PaddlePaddle OCRv4",
        functools.partial(
            download_license_plate_reader_model,
            model_name="ch_PP-OCRv4_rec_infer",
            source_subdir="ch_PP-OCRv4_rec_infer",
            source_stem="ch_PP-OCRv4_rec_infer",
        ),
    ),
    "PP-OCRv5_server_rec": (
        "PaddlePaddle PP-OCRv5_server_rec",
        functools.partial(download_ppocr_v5, model_name="PP-OCRv5_server_rec"),
    ),
    "PP-OCRv5_mobile_rec": (
        "PaddlePaddle PP-OCRv5_mobile_rec",
        functools.partial(download_ppocr_v5, model_name="PP-OCRv5_mobile_rec"),
    ),
    "mars-small128": ("Mars-Small128", download_mars_small128),
    "pallet_defect_detection": ("Pallet Defect Detection", download_pallet_defect_detection),
    "colorcls2": ("Colorcls2", download_colorcls2),
    "pointpillars": ("PointPillars", download_pointpillars),
}


# ================================= CLI =================================


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download and convert DL Streamer models not covered by the Ultralytics/HF/TIMM scripts.",
        epilog="Supported models: " + ", ".join(MODEL_HANDLERS),
    )
    parser.add_argument(
        "model",
        nargs="?",
        default="all",
        help="'all' (default), a single model or a comma-separated list of models",
    )
    parser.add_argument(
        "--outdir",
        default=None,
        help="Root directory for downloaded models (default: $MODELS_PATH)",
    )
    return parser.parse_args()


def resolve_models(selection: str) -> list[str]:
    if selection.strip() == "all":
        return list(MODEL_HANDLERS)

    requested = [name.strip() for name in selection.split(",") if name.strip()]
    unsupported = [name for name in requested if name not in MODEL_HANDLERS]
    if unsupported:
        raise SystemExit(
            f"Unsupported model(s): {', '.join(unsupported)}\n"
            f"Supported models: all, {', '.join(MODEL_HANDLERS)}"
        )
    # Keep the same processing order as the shell script regardless of the input order.
    return [name for name in MODEL_HANDLERS if name in requested]


def main() -> int:
    args = parse_args()
    models = resolve_models(args.model)

    models_path_arg = args.outdir or os.environ.get("MODELS_PATH", "")
    if not models_path_arg:
        log("MODELS_PATH is not specified")
        log("Please set MODELS_PATH env variable or pass --outdir with target path to download models")
        return 1

    models_path = Path(models_path_arg).expanduser().resolve()
    try:
        (models_path / "public").mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        log(f"Cannot create {models_path / 'public'}: {exc}")
        return 1
    if not os.access(models_path / "public", os.W_OK):
        log(f"{models_path / 'public'} is not writable by the current user")
        return 1

    log(f"Models to process: {' '.join(models)}")
    log(f"Downloading models to folder {models_path}.")

    failures: list[str] = []
    skipped: list[str] = []
    for name in models:
        title, handler = MODEL_HANDLERS[name]
        display_header(f"Downloading {title} model")
        start = time.monotonic()
        try:
            handler(models_path)
            log(f"[OK] {name} ({time.monotonic() - start:.0f}s)")
        except ModelSkipped as exc:
            log(f"[WARN] {name} skipped: {exc}")
            skipped.append(name)
        except Exception:  # pylint: disable=broad-exception-caught
            traceback.print_exc()
            log(f"[ERROR] {name} failed ({time.monotonic() - start:.0f}s)")
            failures.append(name)

    if skipped:
        log(f"\nSkipped models: {', '.join(skipped)}")
    if failures:
        log(f"\nFailed models: {', '.join(failures)}")
    if skipped or failures:
        return 1
    log("\nAll requested models are ready.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
