#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# Zero-shot image and video classification sample for gvaclassify (CLIP).
#
#   ./zero_shot_classification.sh [INPUT] [DEVICE] [OUTPUT]
#
# INPUT  : path or URI to an image or video file (default: Pexels video).
# DEVICE : device used for all inference: CPU, GPU (default), NPU, or MULTI:GPU,CPU.
# OUTPUT : video output: display (default), file, fps, json, or display-and-json.
#          Images always output JSON.
#
# The default video is by Moe Magners via Pexels:
# https://www.pexels.com/video/people-giving-a-thumbs-up-7504884/
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
#   python3 download_hf_models.py --model "$CLIP" --export-variant clip-zeroshot --outdir .
#
#   # 2) Text-label embeddings -> labels.safetensors (carries logit_scale).
#   python3 clip_text_embeddings.py --model "$CLIP" --labels labels.txt \
#           --output labels.safetensors
#
# Copy openai_clip-vit-base-patch32/ and labels.safetensors next to this script,
# or point MODEL at the exported .xml file and EMBEDDINGS at the .safetensors file.
# Video input also requires arnabdhar/YOLOv8-Face-Detection at
# ${MODELS_PATH}/public/arnabdhar_YOLOv8-Face-Detection/FP16/
# arnabdhar_YOLOv8-Face-Detection.xml, or set DETECTION_MODEL explicitly.
set -euo pipefail
OUTPUT_DIR=$PWD
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

DEFAULT_INPUT=https://videos.pexels.com/video-files/7504884/7504884-hd_1280_720_25fps.mp4
INPUT=${1:-"$DEFAULT_INPUT"}
DEVICE=${2:-GPU}
OUTPUT=${3:-display}
MODEL=${MODEL:-openai_clip-vit-base-patch32/FP16/openai_clip-vit-base-patch32.xml}
LABELS=labels.txt
EMBEDDINGS=${EMBEDDINGS:-labels.safetensors}
DETECTION_MODEL=${DETECTION_MODEL:-}

if [ -z "$DETECTION_MODEL" ] && [ -n "${MODELS_PATH:-}" ]; then
  DETECTION_MODEL="${MODELS_PATH%/}/public/arnabdhar_YOLOv8-Face-Detection/FP16/arnabdhar_YOLOv8-Face-Detection.xml"
fi

if [ ! -f "$MODEL" ]; then
  echo "Missing model XML: $MODEL" >&2
  echo "MODEL must point to the exported .xml file, not its parent directory." >&2
  echo "Expected layout: <model-directory>/FP16/<model-name>.xml" >&2
  exit 1
fi

if [ ! -f "$EMBEDDINGS" ]; then
  echo "Missing embeddings file: $EMBEDDINGS" >&2
  echo "EMBEDDINGS must point to the generated .safetensors file." >&2
  exit 1
fi

if [[ "$INPUT" == *"://"* ]]; then
  SOURCE=(urisourcebin "uri=${INPUT}" ! decodebin3)
else
  SOURCE=(filesrc "location=${INPUT}" ! decodebin3)
fi

if [[ "$INPUT" == *.jpg || "$INPUT" == *.jpeg || "$INPUT" == *.png || "$INPUT" == *.bmp ]]; then
  INFERENCE=(
    gvaclassify "model=${MODEL}" "labels-file=${LABELS}"
    "zeroshot-embeddings-file=${EMBEDDINGS}" zeroshot-topk=3
    inference-region=full-frame "device=${DEVICE}"
  )
  SINK=(
    gvametaconvert format=json add-tensor-data=false !
    gvametapublish method=file file-path=/dev/stdout ! fakesink sync=false
  )
else
  if [ ! -f "$DETECTION_MODEL" ]; then
    echo "Missing YOLOv8 face detection model XML: ${DETECTION_MODEL:-<not set>}" >&2
    echo "Set DETECTION_MODEL or MODELS_PATH before processing video." >&2
    echo "Expected layout: \${MODELS_PATH}/public/arnabdhar_YOLOv8-Face-Detection/FP16/arnabdhar_YOLOv8-Face-Detection.xml" >&2
    exit 1
  fi

  INFERENCE=(
    gvadetect "model=${DETECTION_MODEL}" "device=${DEVICE}" ! queue !
    gvaclassify "model=${MODEL}" "labels-file=${LABELS}"
    "zeroshot-embeddings-file=${EMBEDDINGS}" zeroshot-topk=1
    inference-region=roi-list "device=${DEVICE}"
  )

  case "$OUTPUT" in
    display)
      SINK=(vapostproc ! gvawatermark ! videoconvertscale ! gvafpscounter ! autovideosink sync=false)
      ;;
    file)
      if gst-inspect-1.0 vah264enc >/dev/null 2>&1; then
        ENCODER=vah264enc
      elif gst-inspect-1.0 vah264lpenc >/dev/null 2>&1; then
        ENCODER=vah264lpenc
      else
        echo "VA-API H.264 encoder not found." >&2
        exit 1
      fi
      OUTPUT_FILE="${OUTPUT_DIR}/output.mp4"
      rm -f "$OUTPUT_FILE"
      SINK=(vapostproc ! gvawatermark ! gvafpscounter ! "$ENCODER" ! h264parse ! mp4mux ! filesink "location=${OUTPUT_FILE}")
      ;;
    fps)
      SINK=(gvafpscounter ! fakesink async=false)
      ;;
    json)
      OUTPUT_FILE="${OUTPUT_DIR}/output.json"
      rm -f "$OUTPUT_FILE"
      SINK=(
        gvametaconvert add-tensor-data=false !
        gvametapublish file-format=json-lines "file-path=${OUTPUT_FILE}" !
        fakesink async=false
      )
      ;;
    display-and-json)
      OUTPUT_FILE="${OUTPUT_DIR}/output.json"
      rm -f "$OUTPUT_FILE"
      SINK=(
        vapostproc ! gvawatermark !
        gvametaconvert add-tensor-data=false !
        gvametapublish file-format=json-lines "file-path=${OUTPUT_FILE}" !
        videoconvertscale ! gvafpscounter ! autovideosink sync=false
      )
      ;;
    *)
      echo "Unsupported video output: $OUTPUT" >&2
      echo "Supported values: display, file, fps, json, display-and-json" >&2
      exit 1
      ;;
  esac
fi

PIPELINE=(gst-launch-1.0 "${SOURCE[@]}" ! "${INFERENCE[@]}" ! "${SINK[@]}")

echo "Pipeline:"
for ARGUMENT in "${PIPELINE[@]}"; do
  if [ "$ARGUMENT" = "!" ]; then
    printf ' !'
  else
    printf ' %q' "$ARGUMENT"
  fi
done
printf '\n'

"${PIPELINE[@]}"
