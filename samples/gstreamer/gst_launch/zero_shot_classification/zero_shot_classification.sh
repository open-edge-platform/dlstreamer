#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# Zero-shot image classification sample for gvaclassify (OpenCLIP).
#
#   ./zero_shot_classification.sh [INPUT] [DEVICE]
#
# INPUT  : path to an image/video file, or an RTSP/`v4l2`/`videotestsrc`-style URI
#          (default: ./images/zebra.jpg if present, otherwise prints guidance).
# DEVICE : CPU (default), GPU, NPU, or MULTI:GPU,CPU.
#
# Prerequisites (one-time, in a Python venv with open_clip_torch, openvino, safetensors):
#   python3 tools/export_clip_vision_ov.py --model ViT-B-32 --pretrained openai --out clip_vision.xml
#   python3 tools/gen_label_embeddings.py  --model ViT-B-32 --pretrained openai \
#           --labels labels.txt --out labels.safetensors
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

INPUT=${1:-"images/zebra.jpg"}
DEVICE=${2:-CPU}
MODEL=clip_vision.xml
MODEL_PROC=model_proc/clip_zeroshot.json
LABELS=labels.txt
EMBEDDINGS=labels.safetensors

for f in "$MODEL" "$EMBEDDINGS"; do
  if [ ! -f "$f" ]; then
    echo "Missing $f. Build the artifacts first (see the header of this script / README.md)."
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
  gvaclassify model="${MODEL}" model-proc="${MODEL_PROC}" labels-file="${LABELS}" \
    zeroshot-embeddings-file="${EMBEDDINGS}" zeroshot-topk=3 \
    inference-region=full-frame device="${DEVICE}" ! \
  ${SINK}
