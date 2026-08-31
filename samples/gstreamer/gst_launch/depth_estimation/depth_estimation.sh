#!/bin/bash
# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# This sample refers to a video file by cottonbro studio via Pexels
# (https://www.pexels.com)
# ==============================================================================

set -euo pipefail

if [ $# -gt 0 ] && ([ "$1" = "--help" ] || [ "$1" = "-h" ]); then
  echo "Usage: $0 [INPUT] [DETECT_DEVICE] [DEPTH_DEVICE] [OUTPUT]"
  echo ""
  echo "Arguments:"
  echo "  INPUT          - Input source (default: Pexels video URL)"
  echo "  DETECT_DEVICE  - Device for YOLO11n detection (default: GPU). Supported: CPU, GPU"
  echo "  DEPTH_DEVICE   - Device for Depth Anything inference (default: GPU). Supported: CPU, GPU"
  echo "  OUTPUT         - Output type (default: display). Supported: display, fps, json, display-and-json"
  echo ""
  echo "Environment:"
  echo "  MODELS_PATH    - Base directory used to resolve default model locations"
  echo ""
  exit 0
fi

validate_device() {
  local label=$1
  local value=$2

  if [[ "$value" != "CPU" && "$value" != "GPU" ]]; then
    echo "Error: ${label} must be CPU or GPU." >&2
    exit 1
  fi
}

fallback_gpu_device() {
  local label=$1
  local value=$2

  if [[ "$value" == "GPU" && ! -e "/dev/dri/renderD128" ]]; then
    echo "WARN: /dev/dri/renderD128 not found, switching ${label} to CPU" >&2
    echo "CPU"
    return 0
  fi

  echo "$value"
}

INPUT=${1:-https://videos.pexels.com/video-files/18553046/18553046-hd_1280_720_30fps.mp4}
DETECT_DEVICE=${2:-GPU}
DEPTH_DEVICE=${3:-GPU}
OUTPUT=${4:-display}

validate_device "DETECT_DEVICE" "$DETECT_DEVICE"
validate_device "DEPTH_DEVICE" "$DEPTH_DEVICE"

DETECT_DEVICE=$(fallback_gpu_device "DETECT_DEVICE" "$DETECT_DEVICE")
DEPTH_DEVICE=$(fallback_gpu_device "DEPTH_DEVICE" "$DEPTH_DEVICE")

if [ -z "${MODELS_PATH:-}" ]; then
  echo "Error: MODELS_PATH is not set. Please set MODELS_PATH." >&2
  exit 1
fi

DETECTION_MODEL="${MODELS_PATH}/public/yolo11n/FP16/yolo11n.xml"
DEPTH_MODEL="${MODELS_PATH}/public/depth-anything_Depth-Anything-V2-Small-hf/FP16/depth-anything_Depth-Anything-V2-Small-hf.xml"

echo "DETECTION_MODEL: ${DETECTION_MODEL}"
echo "DEPTH_MODEL: ${DEPTH_MODEL}"
echo "DETECT_DEVICE: ${DETECT_DEVICE}"
echo "DEPTH_DEVICE: ${DEPTH_DEVICE}"

if [[ $INPUT == "/dev/video"* ]]; then
  SOURCE_ELEMENT="v4l2src device=\"${INPUT}\""
elif [[ $INPUT == *"://"* ]]; then
  SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=\"${INPUT}\""
else
  SOURCE_ELEMENT="filesrc location=\"${INPUT}\""
fi

CONVERSION_ELEMENT=""

if [[ $DETECT_DEVICE == "CPU" ]]; then
  DECODE_ELEMENT="decodebin3"
  DETECT_PREPROC="pre-process-backend=opencv"
  DISPLAY_PREFIX=""
else
  DECODE_ELEMENT="decodebin3"
  DETECT_PREPROC="pre-process-backend=va-surface-sharing"
  DISPLAY_PREFIX="vapostproc ! "

  if [[ $DEPTH_DEVICE == "CPU" ]]; then
    CONVERSION_ELEMENT="vapostproc ! "
    DISPLAY_PREFIX=""
  fi
fi

if [[ $OUTPUT == "display" ]]; then
  SINK_ELEMENT="${DISPLAY_PREFIX}gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "fps" ]]; then
  SINK_ELEMENT="gvafpscounter ! fakesink async=false"
elif [[ $OUTPUT == "json" ]]; then
  SINK_ELEMENT="gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false"
elif [[ $OUTPUT == "display-and-json" ]]; then
  rm -f output.json
  SINK_ELEMENT="${DISPLAY_PREFIX}gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! autovideosink sync=false"
else
  echo "Error: wrong value for OUTPUT parameter" >&2
  echo "Valid values: display, fps, json, display-and-json" >&2
  exit 1
fi

PIPELINE="gst-launch-1.0 ${SOURCE_ELEMENT} ! ${DECODE_ELEMENT} ! \
gvadetect model=\"${DETECTION_MODEL}\" device=${DETECT_DEVICE} ${DETECT_PREPROC} ! queue ! \
${CONVERSION_ELEMENT}queue ! \
gvaclassify model=\"${DEPTH_MODEL}\" device=${DEPTH_DEVICE} pre-process-backend=opencv skip-raw-tensors=true ! queue ! \
${SINK_ELEMENT}"

echo "${PIPELINE}"
eval "$PIPELINE"
