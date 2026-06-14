#!/usr/bin/env python3
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Generate CLIP text-label embeddings for zero-shot gvaclassify.

Reads one class label per line, encodes each with the CLIP text tower, and writes the
[num_classes, embedding_dim] matrix to a .safetensors file under the tensor name "embeddings"
(rows aligned to label order). The model's CLIP logit_scale is stored in the file metadata so the
converter can produce calibrated probabilities; the labels and model id are stored too for
traceability.

    python3 gen_label_embeddings.py --model ViT-B-32 --pretrained openai \\
        --labels labels.txt --out labels.safetensors

Use the SAME --model / --pretrained as export_clip_vision_ov.py.

Requires: open_clip_torch, safetensors (and torch, pulled in by open_clip_torch).
"""
import argparse

import open_clip
import torch
from safetensors.torch import save_file


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="ViT-B-32")
    ap.add_argument("--pretrained", default="openai")
    ap.add_argument("--labels", default="labels.txt")
    ap.add_argument("--out", default="labels.safetensors")
    ap.add_argument("--prompt", default="a photo of a {}",
                    help="Prompt template; '{}' is replaced by each label. Use '{}' for the bare label.")
    args = ap.parse_args()

    with open(args.labels, "r", encoding="utf-8") as f:
        labels = [line.strip() for line in f if line.strip()]
    if not labels:
        raise SystemExit(f"No labels found in {args.labels}")

    # Match export_clip_vision_ov.py exactly: OpenAI weights use QuickGELU. Both towers MUST use the
    # same activation or the image and text embeddings will not share one space.
    force_qgelu = args.pretrained == "openai"
    model, _, _ = open_clip.create_model_and_transforms(
        args.model, pretrained=args.pretrained, force_quick_gelu=force_qgelu
    )
    model.eval()
    tokenizer = open_clip.get_tokenizer(args.model)

    prompts = [args.prompt.format(lbl) for lbl in labels]
    with torch.no_grad():
        tokens = tokenizer(prompts)
        embeddings = model.encode_text(tokens)
        embeddings = embeddings / embeddings.norm(dim=-1, keepdim=True)  # L2-normalize rows
        embeddings = embeddings.to(torch.float32).contiguous()
        logit_scale = float(model.logit_scale.exp().item())

    metadata = {
        "logit_scale": repr(logit_scale),
        "model": f"{args.model}/{args.pretrained}",
        "labels": ",".join(labels),
        "prompt": args.prompt,
    }
    save_file({"embeddings": embeddings}, args.out, metadata=metadata)

    print(f"[gen] {len(labels)} labels -> embeddings {tuple(embeddings.shape)} logit_scale={logit_scale:.2f}")
    print(f"[gen] label order (row i): {labels}")
    print(f"[gen] wrote {args.out}")
    print("[gen] Pass the SAME labels.txt to gvaclassify labels-file= so the reported names match the rows.")


if __name__ == "__main__":
    main()
