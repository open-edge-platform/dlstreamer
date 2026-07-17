# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Hugging Face model download/export helpers."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path
from huggingface_hub import snapshot_download
from hf_utils import custom_conversion
from hf_utils import get_hf_model_support_level
from hf_utils import parse_model_ref



def parse_args() -> argparse.Namespace:
    raw_argv = sys.argv[1:]
    script_options = {"-h", "--help", "--model", "--outdir", "--token", "--extra_args"}
    filtered_argv: list[str] = []
    extracted_extra_args: list[str] = []

    i = 0
    while i < len(raw_argv):
        token = raw_argv[i]
        if token == "--extra_args":
            i += 1
            while i < len(raw_argv) and raw_argv[i] not in script_options:
                extracted_extra_args.append(raw_argv[i])
                i += 1
            continue

        filtered_argv.append(token)
        i += 1

    parser = argparse.ArgumentParser(
        description="Download Hugging Face models and convert them to OpenVINO format."
    )
    parser.add_argument(
        "--model",
        required=True,
        help="Hugging Face model ID",
    )
    parser.add_argument(
        "--outdir",
        default=".",
        help="Output directory for exports",
    )
    parser.add_argument(
        "--token",
        default=None,
        help="Hugging Face token for gated/private models",
    )
    parser.add_argument(
        "--extra_args",
        nargs="*",
        default=[],
        help="Additional arguments to pass to optimum-cli export",
    )

    args = parser.parse_args(filtered_argv)
    if extracted_extra_args:
        args.extra_args.extend(extracted_extra_args)
    return args


def main() -> int:
    args = parse_args()
    model_id = args.model
    token = args.token

    try:
        # Parse model_id to extract repo_id and optional revision
        repo_id, revision = parse_model_ref(model_id)
        
        # Download model with specified revision (or latest if None) to local cache
        print(f"Downloading model: {repo_id}" + (f" @ {revision}" if revision else " (latest)"))
        local_model_dir = snapshot_download(
            repo_id=repo_id,
            revision=revision,
            token=token,
        )
        print(f"Model cached at: {local_model_dir}")
        
        # Determine support level by analyzing locally cached model
        support_level = get_hf_model_support_level(local_model_dir, token)
        
        match support_level:
            case 0:
                # Standard export using optimum-cli
                model_path = Path(args.outdir) / repo_id.replace("/", "_")
                model_path.mkdir(parents=True, exist_ok=True)

                command = [
                    "optimum-cli",
                    "export",
                    "openvino",
                    "--model",
                    local_model_dir,
                ]
                if args.extra_args:
                    command.extend(args.extra_args)
                command.append(str(model_path))
                env = os.environ if not token else {**os.environ, "HF_TOKEN": token}

                subprocess.run(command, check=True, env=env)

            case 1:
                # Custom conversion using locally cached model
                model_path = custom_conversion(
                    local_model_dir,
                    repo_id,
                    Path(args.outdir),
                    token,
                    extra_args=args.extra_args,
                )

            case 2:
                print(f"Model is not supported by DL Streamer: {model_id}")
                return 1
            case _:
                raise ValueError(f"Unexpected support level: {support_level}")

        print(f"Exported model location: {model_path}")
        return 0
        
    except OSError as exc:
        print(f"Error: Model '{model_id}' not found or inaccessible")
        print(f"Details: {str(exc)}")
        return 1
    except subprocess.CalledProcessError as exc:
        print(f"Error during model export: {str(exc)}")
        return 1
    except Exception as exc:
        print(f"Unexpected error: {str(exc)}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
