# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Shared helper functions used by Python sample scripts."""

import shutil
import urllib.request
from urllib.parse import urlparse


def download_https(url, destination, allowed_hosts, timeout=60):
    """Stream an HTTPS URL to ``destination``; reject non-allowlisted hosts."""
    parsed = urlparse(url)
    if parsed.scheme != "https" or parsed.hostname not in allowed_hosts:
        raise ValueError(f"Refusing non-allowlisted URL: {url}")
    with urllib.request.build_opener().open(url, timeout=timeout) as response, open(destination, "wb") as output:
        shutil.copyfileobj(response, output)


def resolve_hf_revision(repo_id):
    """Resolve the current immutable commit SHA for a Hugging Face repo."""
    from huggingface_hub import HfApi  # pylint: disable=import-outside-toplevel

    revision = HfApi().model_info(repo_id).sha
    if not revision:
        raise RuntimeError(f"Unable to resolve Hugging Face revision for {repo_id}")
    return revision
