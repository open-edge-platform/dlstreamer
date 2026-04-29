#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

INPUT=${1:-https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4}
DEVICE=${2:-GPU}
OUTPUT=${3:-fps} # Supported values: display, fps

SCRIPTDIR="$(dirname "$(realpath "$0")")"

# --- Prepare models (download from HuggingFace and convert to OpenVINO IR) ---
echo "Preparing models..."
eval "$(python3 "${SCRIPTDIR}/prepare_models.py")"

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

DETECT_MODEL_PATH=${DETECT_MODEL_PATH:?Detection model not prepared}

echo Running sample with the following parameters:
echo GST_PLUGIN_PATH="${GST_PLUGIN_PATH}"

PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3 ! gvadetect model=$DETECT_MODEL_PATH device=$DEVICE ! queue ! gvaframesaver_py ! $SINK_ELEMENT"

echo "${PIPELINE}"
GST_PLUGIN_PATH="${GST_PLUGIN_PATH}:${SCRIPTDIR}/plugins" \
$PIPELINE
