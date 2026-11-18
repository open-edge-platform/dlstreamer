#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

INPUT=${1:-}
if [[ -z "$INPUT" ]]; then
  echo "Error: provide an input video path, stream URL, or camera device as the first argument." >&2
  exit 2
fi
DEVICE=${2:-GPU}
OUTPUT=${3:-fps} # Supported values: display, fps

SCRIPTDIR="$(dirname "$(realpath "$0")")"

MODEL_NAME="arnabdhar_YOLOv8-Face-Detection"
MODELS_ROOT="${MODELS_PATH:-${SCRIPTDIR}/models}"
DETECT_MODEL_PATH="${DETECT_MODEL_PATH:-${MODELS_ROOT}/public/${MODEL_NAME}/FP16/${MODEL_NAME}.xml}"
if [[ ! -s "$DETECT_MODEL_PATH" ]]; then
  echo "Detection model not found: $DETECT_MODEL_PATH" >&2
  echo "Prepare it with scripts/download_models/download_ultralytics_models.py --model arnabdhar/YOLOv8-Face-Detection --outdir \"${MODELS_ROOT}/public/${MODEL_NAME}/FP16\" --half" >&2
  exit 1
fi

if [[ $OUTPUT == "display" ]] || [[ -z $OUTPUT ]]; then
  SINK_ELEMENT="gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "fps" ]]; then
  SINK_ELEMENT="gvafpscounter ! fakesink async=false"
else
  echo Error wrong value for OUTPUT parameter
  echo Valid values: "display" - render to screen, "fps" - print FPS
  exit
fi

if [[ $INPUT == "/dev/video"* ]]; then
  SOURCE_ELEMENT="v4l2src device=${INPUT}"
elif [[ $INPUT == *"://"* ]]; then
  SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${INPUT}"
else
  SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

echo Running sample with the following parameters:
echo GST_PLUGIN_PATH="${GST_PLUGIN_PATH}"

PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3 ! gvadetect model=$DETECT_MODEL_PATH device=$DEVICE ! queue ! gvaframesaver_py ! $SINK_ELEMENT"

echo "${PIPELINE}"
GST_PLUGIN_PATH="${GST_PLUGIN_PATH}:${SCRIPTDIR}/plugins" \
$PIPELINE
