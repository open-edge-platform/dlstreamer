# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Shared helper functions used by sample and model download scripts."""

from __future__ import annotations

import shutil
import urllib.request
from pathlib import Path
from urllib.parse import urlparse

# Some CDNs reject the default Python-urllib agent with HTTP 403,
# so present a common browser User-Agent instead.
_USER_AGENT = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
)


def download_https(url: str, destination: str | Path, allowed_hosts: set[str], timeout: int = 60) -> None:
    """Stream an HTTPS URL to destination; reject non-allowlisted hosts."""
    parsed = urlparse(url)
    if parsed.scheme != "https" or parsed.hostname not in allowed_hosts:
        raise ValueError(f"Refusing non-allowlisted URL: {url}")

    request = urllib.request.Request(url, headers={"User-Agent": _USER_AGENT})
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.build_opener().open(request, timeout=timeout) as response, destination.open("wb") as output:
        shutil.copyfileobj(response, output)


def resolve_hf_revision(repo_id: str) -> str:
    """Resolve the current immutable commit SHA for a Hugging Face repo."""
    from huggingface_hub import HfApi  # pylint: disable=import-outside-toplevel

    revision = HfApi().model_info(repo_id).sha
    if not revision:
        raise RuntimeError(f"Unable to resolve Hugging Face revision for {repo_id}")
    return revision
