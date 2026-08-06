#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# Zero-shot image classification sample for gvaclassify (CLIP).
#
#   ./zero_shot_classification.sh [INPUT] [DEVICE]
#
# INPUT  : path to an image/video file, or a v4l2/videotestsrc-style URI
#          (default: ./images/zebra.jpg if present).
# DEVICE : CPU (default), GPU, NPU, or MULTI:GPU,CPU.
#
# Prerequisites (one-time). In the scripts/download_models Python environment
# (see scripts/download_models/README.md), prepare the two artifacts with the
# SAME CLIP model so the image and text embeddings share one space:
#
#   CLIP=openai/clip-vit-base-patch32
#
#   # 1) CLIP image encoder -> OpenVINO IR (projected image embedding).
#   #    Preprocessing is written into the model_info section of model.xml,
#   #    so no DL Streamer model-proc file is needed.
#   python3 download_hf_models.py --model "$CLIP" --extra_args --zeroshot --outdir .
#
#   # 2) Text-label embeddings -> labels.safetensors (carries logit_scale).
#   python3 clip_text_embeddings.py --model "$CLIP" --labels labels.txt \
#           --output labels.safetensors
#
# Copy clip-vit-base-patch32/ and labels.safetensors next to this script, or
# point MODEL/EMBEDDINGS at them.
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

INPUT=${1:-"images/zebra.jpg"}
DEVICE=${2:-CPU}
MODEL=${MODEL:-clip-vit-base-patch32/clip-vit-base-patch32.xml}
LABELS=labels.txt
EMBEDDINGS=${EMBEDDINGS:-labels.safetensors}

for f in "$MODEL" "$EMBEDDINGS"; do
  if [ ! -f "$f" ]; then
    echo "Missing $f. Prepare the artifacts first (see the header of this script / README.md)."
    exit 1
  fi
done

if [[ "$INPUT" == *.jpg || "$INPUT" == *.jpeg || "$INPUT" == *.png || "$INPUT" == *.bmp ]]; then
  SOURCE="filesrc location=${INPUT} ! decodebin3 ! videoconvert"
  SINK="gvametaconvert format=json add-tensor-data=false ! gvametapublish method=file file-path=/dev/stdout ! fakesink sync=false"
else
  SOURCE="filesrc location=${INPUT} ! decodebin3 ! videoconvert"
  SINK="gvawatermark ! videoconvert ! autovideosink sync=false"
fi

set -x
gst-launch-1.0 \
  ${SOURCE} ! \
  gvaclassify model="${MODEL}" labels-file="${LABELS}" \
    zeroshot-embeddings-file="${EMBEDDINGS}" zeroshot-topk=3 \
    inference-region=full-frame device="${DEVICE}" ! \
  ${SINK}
