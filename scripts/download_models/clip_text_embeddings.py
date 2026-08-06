# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Generate CLIP text-label embeddings for zero-shot classification.

Reads a label list, runs the CLIP text encoder for each label (wrapped in a prompt
template), L2-normalizes the projected text embeddings, and writes them to a
``.safetensors`` file consumed by the gvaclassify ``zeroshot_openclip`` converter.

The output tensor ``embeddings`` has shape ``[num_labels, embedding_dim]`` with rows
aligned to the label order. File metadata carries:

- ``logit_scale``: the CLIP temperature (``exp`` of the model's ``logit_scale``) applied
  before the softmax so the reported confidences are calibrated.
- ``model``, ``prompt``, ``labels``: provenance.
- ``unknown_threshold`` (optional): top-1 cosine similarity below which a result is
  labelled ``unknown``.

Use the same CLIP model here and for the image encoder exported with
``download_hf_models.py --model <clip_id> --extra_args --zeroshot`` so the image and
text embeddings share one space.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import torch
from safetensors.torch import save_file
from transformers import AutoProcessor, CLIPModel

from hf_utils import resolve_hf_model_ref


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate CLIP text-label embeddings (.safetensors) for zero-shot classification."
    )
    parser.add_argument(
        "--model",
        required=True,
        help="CLIP Hugging Face model ID or repo_id@revision override",
    )
    parser.add_argument(
        "--labels",
        required=True,
        help="Path to a labels file, one label per line",
    )
    parser.add_argument(
        "--output",
        default="labels.safetensors",
        help="Output .safetensors path",
    )
    parser.add_argument(
        "--prompt",
        default="a photo of a {}",
        help="Prompt template; {} is replaced by each label",
    )
    parser.add_argument(
        "--token",
        default=None,
        help="Hugging Face token for gated/private models",
    )
    parser.add_argument(
        "--unknown-threshold",
        type=float,
        default=None,
        help="Optional top-1 cosine similarity below which a result is labelled 'unknown'",
    )
    return parser.parse_args()


def read_labels(path: Path) -> list[str]:
    labels = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()]
    labels = [label for label in labels if label]
    if not labels:
        raise ValueError(f"No labels found in {path}")
    return labels


def main() -> int:
    args = parse_args()
    repo_id, revision = resolve_hf_model_ref(args.model, args.token)
    if "@" not in args.model:
        print(f"Resolved {repo_id} to revision {revision}")

    labels = read_labels(Path(args.labels))
    prompts = [args.prompt.format(label) for label in labels]

    model = CLIPModel.from_pretrained(repo_id, revision=revision, token=args.token)
    model.eval()
    processor = AutoProcessor.from_pretrained(repo_id, revision=revision, token=args.token)

    text_inputs = processor.tokenizer(prompts, padding=True, return_tensors="pt")
    with torch.no_grad():
        text_features = model.get_text_features(**text_inputs)
        text_features = torch.nn.functional.normalize(text_features, dim=-1)

    embeddings = text_features.contiguous().to(torch.float32)

    logit_scale = float(model.logit_scale.exp().item())
    metadata = {
        "logit_scale": f"{logit_scale:.6f}",
        "model": repo_id,
        "prompt": args.prompt,
        "labels": ",".join(labels),
    }
    if args.unknown_threshold is not None:
        metadata["unknown_threshold"] = f"{args.unknown_threshold:.6f}"

    output_path = Path(args.output)
    if output_path.parent != Path(""):
        output_path.parent.mkdir(parents=True, exist_ok=True)
    save_file({"embeddings": embeddings}, str(output_path), metadata=metadata)

    print(
        f"Wrote {embeddings.shape[0]} label embeddings "
        f"(dim {embeddings.shape[1]}) to {output_path}"
    )
    print(f"logit_scale={logit_scale:.3f}, model={repo_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
