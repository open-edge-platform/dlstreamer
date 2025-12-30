#!/bin/bash
# ==============================================================================
# Copyright (C) 2021-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# This sample refers to a video file by Media Hopper Studio via Pexels
# (https://www.pexels.com)
# ==============================================================================

set -euo pipefail

if [ -z "${MODELS_PATH:-}" ]; then
  echo "Error: MODELS_PATH is not set." >&2 
  exit 1
else 
  echo "MODELS_PATH: $MODELS_PATH"
fi

# List help message
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: $0 [INPUT] [DEVICE] [OUTPUT]"
  echo ""
  echo "Arguments:"
  echo "  INPUT     - Input source (default: Pexels video URL)"
  echo "  DEVICE    - Device (default: GPU). Supported: CPU, GPU"
  echo "  OUTPUT    - Output type (default: fps). Supported: display, display-async, fps, json, display-and-json, file"
  echo ""
  exit 0
fi

# Command-line parameters
INPUT=${1:-https://github.com/open-edge-platform/edge-ai-resources/raw/main/videos/ParkingVideo.mp4}
DEVICE=${2:-GPU}     # Device for decode and inference in OpenVINO(TM) format, examples: AUTO, CPU, GPU, GPU.0
OUTPUT=${3:-fps}     # Output type, valid values: display, fps, json, display-and-json, file


if [ ! -e "/dev/dri/renderD128" ]; then
  DEVICE="CPU"
fi

echo "DEVICE is set to: $DEVICE"

# Models
DETECTION_MODEL=${MODELS_PATH}/public/yolov8_license_plate_detector/FP32/yolov8_license_plate_detector.xml
OCR_CLASSIFICATION_MODEL=${MODELS_PATH}/public/ch_PP-OCRv4_rec_infer/FP32/ch_PP-OCRv4_rec_infer.xml

# Check if model exists in local directory
if [ ! -f "$DETECTION_MODEL" ]; then
    echo "ERROR - model not found: $DETECTION_MODEL" >&2
    exit 1
fi

# Check if model exists in local directory
if [ ! -f "$OCR_CLASSIFICATION_MODEL" ]; then
    echo "ERROR - model not found: $OCR_CLASSIFICATION_MODEL" >&2
    exit 1
fi

if [[ $INPUT == "/dev/video"* ]]; then
  SOURCE_ELEMENT="v4l2src device=${INPUT}"
elif [[ $INPUT == *"://"* ]]; then
  SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${INPUT}"
else
  SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

if [[ $DEVICE == "CPU" ]]; then
  DECODE_ELEMENT="decodebin3 "
  PREPROC="pre-process-backend=opencv"
elif [[ $DEVICE == "GPU" ]]; then
  DECODE_ELEMENT="decodebin3 ! vapostproc ! video/x-raw\(memory:VAMemory\)"
  PREPROC="pre-process-backend=va-surface-sharing"
else
  DECODE_ELEMENT="decodebin3"
  PREPROC=""
fi

if [[ $OUTPUT == "display" ]]; then
  SINK_ELEMENT="vapostproc ! gvawatermark ! videoconvert ! gvafpscounter ! autovideosink"
elif [[ $OUTPUT == "display-async" ]]; then
  SINK_ELEMENT="vapostproc ! gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "fps" ]]; then
  SINK_ELEMENT="gvafpscounter ! fakesink async=false "
elif [[ $OUTPUT == "json" ]]; then
  rm -f output.json
  SINK_ELEMENT="gvametaconvert ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false"
elif [[ $OUTPUT == "display-and-json" ]]; then
  rm -f output.json
  SINK_ELEMENT="vapostproc ! gvawatermark ! gvametaconvert ! gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "file" ]]; then
  FILE="$(basename ${INPUT%.*})"
  rm -f "lpr_${FILE}_${DEVICE}.mp4"
  if [[ $(gst-inspect-1.0 va | grep vah264enc) ]]; then
    ENCODER="vah264enc"
  elif [[ $(gst-inspect-1.0 va | grep vah264lpenc) ]]; then
    ENCODER="vah264lpenc"
  else
    echo "Error - VA-API H.264 encoder not found."
    exit
  fi
  SINK_ELEMENT="vapostproc ! gvawatermark ! gvafpscounter ! ${ENCODER} ! h264parse ! mp4mux ! filesink location=lpr_${FILE}_${DEVICE}.mp4"
else
  echo Error wrong value for OUTPUT parameter
  echo Valid values: "display" - render to screen, "fps" - print FPS, "json" - write to output.json, "display-and-json" - render to screen and write to output.json
  exit
fi


PIPELINE="gst-launch-1.0 ${SOURCE_ELEMENT} ! ${DECODE_ELEMENT} ! queue ! \
gvadetect model=$DETECTION_MODEL device=${DEVICE} ${PREPROC} ! queue ! videoconvert ! \
gvaclassify model=$OCR_CLASSIFICATION_MODEL device=${DEVICE} ${PREPROC} ! queue !  $SINK_ELEMENT"

echo "${PIPELINE}"
eval "$PIPELINE"


