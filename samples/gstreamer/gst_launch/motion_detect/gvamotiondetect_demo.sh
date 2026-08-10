#!/usr/bin/env bash
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# This sample refers to a video file by Rihard-Clement-Ciprian Diac via Pexels
# (https://www.pexels.com)
# ==============================================================================
#
# Demonstration script for gvamotiondetect element with optional YOLOv8 detection.
# Provides GPU (VA surface memory) and CPU (system memory) pipeline variants.
#
# Default source: https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4
# Default model: yolov8n (precision FP32) resolved via MODELS_PATH:
#   $MODELS_PATH/public/yolov8n/FP32/yolov8n.xml
#
# The motion detector publishes ROIs; gvadetect uses inference-region=1 to
# restrict object detection over those motion ROIs only (region id 1).
#
# Usage:
#   ./gvamotiondetect_demo.sh [--device GPU|CPU] [--source <video|uri>] [--model <xml>] [--precision FP32|FP16|INT8] [--output display|json] [--md-opts "prop1=val prop2=val"]
# Examples:
#   ./gvamotiondetect_demo.sh --device GPU
#   ./gvamotiondetect_demo.sh --device CPU --source /path/to/video.mp4 --md-opts "motion-threshold=0.07 min-persistence=2"
#   ./gvamotiondetect_demo.sh --output json --md-opts "motion-threshold=0.05"  # writes output.json
#
# Notes:
# - decodebin3 should negotiate VAMemory caps if hw decode is available.
# - Ensure gst-video-analytics plugins are discoverable (gst-inspect-1.0 gvamotiondetect).
# - Set GST_DEBUG=gvamotiondetect:4 for verbose motion detection logs.

set -euo pipefail

DEVICE="GPU"
SRC="https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4"
MODEL=""  # if empty we construct from MODELS_PATH
MODEL_NAME="yolov8n"
PRECISION="FP16"
MD_OPTS=""  # additional gvamotiondetect properties, space separated key=value pairs
OUTPUT="json"  # display or json

usage() {
  grep '^#' "$0" | sed 's/^# //'
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --device)
      DEVICE="$2"; shift 2;;
    --source|--src)
      SRC="$2"; shift 2;;
    --model)
      MODEL="$2"; shift 2;;
    --precision)
      PRECISION="$2"; shift 2;;
    --md-opts)
      MD_OPTS="$2"; shift 2;;
    --output)
      OUTPUT="$2"; shift 2;;
    -h|--help)
      usage;;
    *) echo "Unknown arg: $1"; usage;;
  esac
done

# If source looks like a local path ensure it exists; skip existence check for URIs.
if [[ "$SRC" != *"://"* ]] && [[ ! -f "$SRC" ]]; then
  echo "Source file not found: $SRC" >&2
  exit 1
fi
# Resolve default model path using MODELS_PATH if user did not supply --model
if [[ -z "$MODEL" ]]; then
  if [[ -z "${MODELS_PATH:-}" ]]; then
    echo "MODELS_PATH not set; export MODELS_PATH or pass --model <path to yolov8n.xml>" >&2
    exit 1
  fi
  MODEL="$MODELS_PATH/public/$MODEL_NAME/$PRECISION/$MODEL_NAME.xml"
fi

if [[ ! -f "$MODEL" ]]; then
  echo "Model file not found: $MODEL" >&2
  echo "Expected path pattern: $MODELS_PATH/public/$MODEL_NAME/$PRECISION/$MODEL_NAME.xml" >&2
  exit 1
fi

if ! command -v gst-launch-1.0 >/dev/null; then
  echo "gst-launch-1.0 not found in PATH" >&2
  exit 1
fi
if ! gst-inspect-1.0 gvamotiondetect >/dev/null 2>&1; then
  echo "gvamotiondetect plugin not found (check plugin install path)" >&2
  exit 1
fi

# Capture motion detect properties (space-separated key=value) as array tokens
MD_PROP_STRING="${MD_OPTS}"
IFS=' ' read -r -a MD_PROP_ARRAY <<< "${MD_PROP_STRING}"

echo "Running gvamotiondetect demo";
echo " Device : $DEVICE";
echo " Source : $SRC";
echo " Model  : $MODEL";
echo " Precision: $PRECISION";
echo " Output : $OUTPUT";
echo " MD opts: ${MD_PROP_STRING:-<none>}";
echo " Press Ctrl+C to stop.";

if [[ "$SRC" == *"://"* ]]; then
  BASE_PIPE=(urisourcebin buffer-size=4096 "uri=$SRC" ! decodebin3)
else
  BASE_PIPE=(filesrc "location=$SRC" ! decodebin3)
fi

GVAMD=(gvamotiondetect "${MD_PROP_ARRAY[@]}")
if [[ "$DEVICE" == "GPU" ]]; then
  GVADET=(gvadetect "model=$MODEL" device=GPU inference-region=1)
else
  GVADET=(gvadetect "model=$MODEL" device=CPU pre-process-backend=opencv inference-region=1)
fi
if [[ "$OUTPUT" == "json" ]]; then
  rm -f output.json
  # Insert '!' tokens to separate elements; properties remain adjacent to their element.
  TAIL=(gvametaconvert format=json ! gvametapublish method=file file-format=json-lines file-path=output.json ! gvafpscounter ! fakesink)
else
  TAIL=(gvafpscounter ! gvawatermark ! vapostproc ! autovideosink)
fi

if [[ "$DEVICE" == "GPU" ]]; then
  CAPS=(video/x-raw\(memory:VAMemory\))
else
  CAPS=(video/x-raw\(memory:SystemMemory\))
fi

# Assemble full pipeline tokens array
PIPELINE=("${BASE_PIPE[@]}" ! "${CAPS[@]}" ! "${GVAMD[@]}" ! "${GVADET[@]}" ! "${TAIL[@]}")

echo "Launching pipeline:";
printf 'gst-launch-1.0 -e'; for t in "${PIPELINE[@]}"; do printf ' %s' "$t"; done; printf '\n' 
echo
exec gst-launch-1.0 -e "${PIPELINE[@]}"
