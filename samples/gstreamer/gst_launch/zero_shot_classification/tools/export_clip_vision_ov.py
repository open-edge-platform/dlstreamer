#!/usr/bin/env python3
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""Export the CLIP IMAGE encoder (vision tower + visual projection) to OpenVINO IR.

The exported model takes a [1, 3, H, W] image tensor and outputs a single
[1, embedding_dim] image embedding in the shared CLIP image/text space. gvaclassify
runs this model; the zeroshot_openclip converter computes cosine similarity against the
label embeddings produced by gen_label_embeddings.py.

    python3 export_clip_vision_ov.py --model ViT-B-32 --pretrained openai --out clip_vision.xml

Use the SAME --model / --pretrained here and in gen_label_embeddings.py so the image and
text towers share one embedding space.

Requires: open_clip_torch, openvino (and torch, pulled in by open_clip_torch).
"""
import argparse

import open_clip
import openvino as ov
import torch


class ImageEncoder(torch.nn.Module):
    def __init__(self, clip_model):
        super().__init__()
        self.clip_model = clip_model

    def forward(self, x):
        # encode_image() applies the visual projection -> shared space.
        # No L2-normalization here; the converter normalizes at runtime.
        return self.clip_model.encode_image(x)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="ViT-B-32")
    ap.add_argument("--pretrained", default="openai")
    ap.add_argument("--image-size", type=int, default=224)
    ap.add_argument("--out", default="clip_vision.xml")
    args = ap.parse_args()

    # OpenAI CLIP checkpoints were trained with QuickGELU; match it to avoid the open_clip
    # "QuickGELU mismatch" warning and the accuracy drift it causes. gen_label_embeddings.py
    # applies the identical rule.
    force_qgelu = args.pretrained == "openai"
    model, _, _ = open_clip.create_model_and_transforms(
        args.model, pretrained=args.pretrained, force_quick_gelu=force_qgelu
    )
    model.eval()

    enc = ImageEncoder(model).eval()
    example = torch.zeros(1, 3, args.image_size, args.image_size, dtype=torch.float32)
    with torch.no_grad():
        embed_dim = int(enc(example).shape[-1])
    print(f"[export] {args.model}/{args.pretrained} input=1x3x{args.image_size}x{args.image_size} "
          f"embed_dim={embed_dim} quick_gelu={force_qgelu}")

    # CLIP attention makes torch.jit's trace sanity-check report spurious "graphs differed"
    # (non-deterministic SDPA backend choice). Trace once with the check off, then convert; fall
    # back to ONNX if the direct conversion trips on an op.
    try:
        with torch.no_grad():
            traced = torch.jit.trace(enc, example, check_trace=False, strict=False)
        ov_model = ov.convert_model(traced, example_input=example)
        print("[export] converted via torch.jit.trace(check_trace=False)")
    except Exception as exc:  # noqa: BLE001
        onnx_path = (args.out[:-4] if args.out.endswith(".xml") else args.out) + ".onnx"
        print(f"[export] traced convert failed ({type(exc).__name__}: {exc}); falling back to ONNX")
        torch.onnx.export(enc, example, onnx_path, input_names=["image"],
                          output_names=["image_embedding"], opset_version=17)
        ov_model = ov.convert_model(onnx_path)
        print(f"[export] converted via ONNX ({onnx_path})")

    # Pin a fully static input shape. DL Streamer reads W/H from the model to size its resize;
    # a dynamic dim there makes the resize target 0. A ViT only accepts its trained size anyway.
    ov_model.reshape([1, 3, args.image_size, args.image_size])
    print(f"[export] input shape pinned to {ov_model.input(0).partial_shape}")

    ov.save_model(ov_model, args.out)
    print(f"[export] wrote {args.out} (+ .bin); embed_dim={embed_dim} must match the label embeddings.")


if __name__ == "__main__":
    main()
