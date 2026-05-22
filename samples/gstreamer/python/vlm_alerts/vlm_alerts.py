# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#!/usr/bin/env python3
"""
Run a DLStreamer VLM pipeline on a video and export JSON and MP4 results.
"""

import argparse
import os
import subprocess  # nosec B404
import sys
import tempfile
import urllib.request
import urllib.parse
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstPbutils", "1.0")
from gi.repository import Gst, GLib, GstPbutils  # pylint: disable=no-name-in-module, wrong-import-position

BASE_DIR = Path(__file__).resolve().parent

class VLMAlertsError(Exception):
    """Domain-specific exception for VLM Alerts failures."""

@dataclass
class PipelineConfig:
    video: Path
    model: Path
    prompt: str
    device: str
    max_tokens: int
    num_beams: int
    frame_rate: float
    results_dir: Path


def validate_url(url: str) -> bool:
    """Validate URL to ensure it uses safe schemes and trusted domains."""
    try:
        parsed = urllib.parse.urlparse(url)
        # Allow only HTTP and HTTPS schemes
        if parsed.scheme not in ['http', 'https']:
            return False
        # Ensure hostname is present
        if not parsed.netloc:
            return False
        # Block localhost and private IP ranges
        hostname = parsed.netloc.split(':')[0].lower()
        if hostname in ['localhost', '127.0.0.1', '0.0.0.0'] or hostname.startswith(('192.168.', '10.', '172.')):
            return False
        return True
    except Exception:
        return False


def download_video(url: str, target_path: Path) -> None:
    """Return a local video path, downloading it if needed."""
    if not validate_url(url):
        raise VLMAlertsError(f"Invalid or unsafe video URL: {url}")
    
    request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:  # nosec B310
            if hasattr(response, "status") and response.status != 200:
                raise VLMAlertsError(f"Video download failed: HTTP {response.status}")
            
            # Check content length if available
            content_length = response.headers.get('Content-Length')
            if content_length and int(content_length) > 500 * 1024 * 1024:  # 500MB limit
                raise VLMAlertsError(f"Video file too large: {int(content_length) / (1024*1024):.2f} MB")
            
            data = response.read()
            if not data:
                raise VLMAlertsError("Video download failed: empty response")
            
            # Additional size check after download
            if len(data) > 500 * 1024 * 1024:  # 500MB limit
                raise VLMAlertsError(f"Downloaded video too large: {len(data) / (1024*1024):.2f} MB")
            
            with open(target_path, "wb") as file:
                file.write(data)
    except Exception as error:
        if target_path.exists():
            target_path.unlink()  # Clean up on failure
        raise VLMAlertsError(f"Video download failed: {error}") from error


def validate_video(video_path: Path) -> None:
    """Raise VLMAlertsError if the file is missing, empty, or not a valid media file."""
    if not video_path.exists() or video_path.stat().st_size == 0:
        raise VLMAlertsError("Video file is missing or empty")

    Gst.init(None)
    try:
        discoverer = GstPbutils.Discoverer.new(5 * Gst.SECOND)
        info = discoverer.discover_uri(video_path.as_uri())
    except GLib.Error as error:
        raise VLMAlertsError(f"GStreamer discovery failed: {error}") from error

    if info.get_result() != GstPbutils.DiscovererResult.OK:
        raise VLMAlertsError(f"Unsupported media: {info.get_result()}")

    if not info.get_stream_list():
        raise VLMAlertsError("No valid streams found in media file")


def resolve_video(
    video_path: Optional[str],
    video_url: Optional[str],
    videos_dir: Path,
) -> Path:
    if video_path:
        path = Path(video_path).resolve()
        if not path.exists():
            raise VLMAlertsError("Provided --video-path does not exist")
        validate_video(path)
        return path

    videos_dir.mkdir(parents=True, exist_ok=True)
    filename = video_url.rstrip("/").split("/")[-1]
    local_path = videos_dir / filename

    if not local_path.exists():
        print(f"[video] downloading {video_url}")
        download_video(video_url, local_path)

    validate_video(local_path)
    return local_path.resolve()


def validate_hf_model_id(model_id: str) -> bool:
    """Validate Hugging Face model ID format."""
    if not model_id or '/' not in model_id:
        return False
    parts = model_id.split('/')
    if len(parts) != 2:
        return False
    username, model_name = parts
    # Allow alphanumeric, hyphens, underscores, dots
    if not all(c.isalnum() or c in '-_.' for c in username + model_name):
        return False
    return True


def validate_command_args(command: list[str]) -> bool:
    """Validate command arguments to prevent injection attacks."""
    # Check if command starts with expected executable
    if not command or command[0] != "optimum-cli":
        return False
    
    # Validate that all arguments are safe
    for arg in command:
        # Check for shell metacharacters that could be dangerous
        if any(dangerous in str(arg) for dangerous in [';', '&', '|', '`', '$', '(', ')', '<', '>']):
            return False
    
    return True


def resolve_model(
    model_id: Optional[str],
    model_path: Optional[str],
    models_dir: Path,
) -> Path:
    """Return a local OpenVINO model directory, exporting it if needed."""
    if model_path:
        path = Path(model_path).resolve()
        if not path.exists():
            raise VLMAlertsError("Provided --model-path does not exist")
        return path

    # Validate model ID format
    if not validate_hf_model_id(model_id):
        raise VLMAlertsError(f"Invalid Hugging Face model ID format: {model_id}")

    models_dir.mkdir(parents=True, exist_ok=True)
    model_name = model_id.split("/")[-1]
    output_dir = models_dir / model_name

    if output_dir.exists() and any(output_dir.glob("*.xml")):
        print(f"[model] using cached {output_dir}")
        return output_dir.resolve()

    command = [
        "optimum-cli",
        "export",
        "openvino",
        "--model",
        model_id,
        "--task",
        "image-text-to-text",
        "--trust-remote-code",
        str(output_dir),
    ]

    # Validate command before execution
    if not validate_command_args(command):
        raise VLMAlertsError("Invalid command arguments detected")

    try:
        subprocess.run(  # nosec B603
            command, 
            check=True,
            shell=False,  # Explicitly disable shell
            timeout=1800  # 30 minute timeout
        )
    except subprocess.CalledProcessError as error:
        raise VLMAlertsError(
            f"OpenVINO export failed with return code {error.returncode}"
        ) from error
    except subprocess.TimeoutExpired as error:
        raise VLMAlertsError(
            f"OpenVINO export timed out after {error.timeout} seconds"
        ) from error

    if not any(output_dir.glob("*.xml")):
        raise VLMAlertsError("OpenVINO export failed: no XML files found")

    return output_dir.resolve()
