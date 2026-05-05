#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

INPUT=${1:-https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4}
DEVICE=${2:-CPU}
OUTPUT=${3:-display} # Supported values: display, fps, json, display-and-json

SCRIPTDIR="$(dirname "$(realpath "$0")")"

# --- Prepare models (download from HuggingFace and convert to OpenVINO IR) ---
echo "Preparing models..."
eval "$(python3 "${SCRIPTDIR}/prepare_models.py")"

if [[ $OUTPUT == "display" ]] || [[ -z $OUTPUT ]]; then
  SINK_ELEMENT="gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "fps" ]]; then
  SINK_ELEMENT="gvafpscounter ! fakesink async=false"
elif [[ $OUTPUT == "json" ]]; then
  rm -f output.json
  SINK_ELEMENT="gvametaconvert ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false"
elif [[ $OUTPUT == "display-and-json" ]]; then
  rm -f output.json
  SINK_ELEMENT="gvawatermark ! gvametaconvert ! gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "file" ]]; then
  FILE="$(basename ${INPUT%.*})"
  rm -f "face_detection_and_classification_${FILE}_${DEVICE}.mp4"
  if [[ $(gst-inspect-1.0 va | grep vah264enc) ]]; then
    ENCODER="vah264enc"
  elif [[ $(gst-inspect-1.0 va | grep vah264lpenc) ]]; then
    ENCODER="vah264lpenc"
  else
    echo "Error - VA-API H.264 encoder not found."
    exit
  fi
  SINK_ELEMENT="gvawatermark ! gvafpscounter ! ${ENCODER} ! avimux name=mux ! filesink location=face_detection_and_classification_${FILE}_${DEVICE}.mp4"
else
  echo Error wrong value for OUTPUT parameter
  echo Valid values: "display" - render to screen, "file" - render to file, "fps" - print FPS, "json" - write to output.json, "display-and-json" - render to screen and write to output.json
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
CLASS_MODEL_PATH=${CLASS_MODEL_PATH:?Classification model not prepared}
GENDER_MODEL_PATH=${GENDER_MODEL_PATH:?Gender model not prepared}

echo Running sample with the following parameters:
echo GST_PLUGIN_PATH="${GST_PLUGIN_PATH}"

PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3 ! \
gvadetect model=$DETECT_MODEL_PATH device=$DEVICE ! queue ! \
gvaclassify model=$CLASS_MODEL_PATH device=$DEVICE ! queue ! \
gvaclassify model=$GENDER_MODEL_PATH device=$DEVICE ! queue ! \
gvaagelogger_py log-file-path=/tmp/age_log.txt ! \
$SINK_ELEMENT"

echo "${PIPELINE}"
GST_PLUGIN_PATH="${GST_PLUGIN_PATH}:${SCRIPTDIR}/plugins" \
$PIPELINE
