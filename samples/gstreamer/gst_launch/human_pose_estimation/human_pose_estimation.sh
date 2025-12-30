#!/bin/bash
# ==============================================================================
# Copyright (C) 2021-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
set -e

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
  echo "  DEVICE    - Device (default: CPU). Supported: CPU, GPU"
  echo "  OUTPUT    - Output type (default: display). Supported: file, display, fps, json, display-and-json"
  echo ""
  exit 0
fi

INPUT=${1:-https://github.com/intel-iot-devkit/sample-videos/raw/master/face-demographics-walking.mp4}
DEVICE=${2:-CPU}
OUTPUT=${3:-display} # Supported values: display, fps, json, display-and-json

HPE_MODEL=human-pose-estimation-0001

if [[ $INPUT == "/dev/video"* ]]; then
  SOURCE_ELEMENT="v4l2src device=${INPUT}"
elif [[ $INPUT == *"://"* ]]; then
  SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${INPUT}"
else
  SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

if [[ $OUTPUT == "display" ]] || [[ -z $OUTPUT ]]; then
  SINK_ELEMENT="vapostproc ! gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "fps" ]]; then
  SINK_ELEMENT="gvafpscounter ! fakesink async=false "
elif [[ $OUTPUT == "json" ]]; then
  rm -f output.json
  SINK_ELEMENT="gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false "
elif [[ $OUTPUT == "display-and-json" ]]; then
  rm -f output.json
  SINK_ELEMENT="vapostproc ! gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "file" ]]; then
  FILE="$(basename ${INPUT%.*})"
  rm -f "human_pose_estimation_${FILE}_${DEVICE}.mp4"
  if [[ $(gst-inspect-1.0 va | grep vah264enc) ]]; then
    ENCODER="vah264enc"
  elif [[ $(gst-inspect-1.0 va | grep vah264lpenc) ]]; then
    ENCODER="vah264lpenc"
  else
    echo "Error - VA-API H.264 encoder not found."
    exit
  fi
  SINK_ELEMENT="vapostproc ! gvawatermark ! gvafpscounter ! ${ENCODER} ! avimux name=mux ! filesink location=human_pose_estimation_${FILE}_${DEVICE}.mp4"
else
  echo Error wrong value for OUTPUT parameter
  echo Valid values: "display" - render to screen, "file" - render to file, "fps" - print FPS, "json" - write to output.json, "display-and-json" - render to screen and write to output.json
  exit
fi
PROC_PATH() {
    echo "$(dirname "$0")"/model_proc/"$1".json
}

HPE_MODEL_PATH=${MODELS_PATH}/intel/${HPE_MODEL}/FP32/${HPE_MODEL}.xml
HPE_MODEL_PROC=$(PROC_PATH $HPE_MODEL)

PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3 ! \
gvaclassify model=$HPE_MODEL_PATH model-proc=$HPE_MODEL_PROC device=$DEVICE inference-region=full-frame ! queue ! \
$SINK_ELEMENT"

echo "${PIPELINE}"
$PIPELINE
