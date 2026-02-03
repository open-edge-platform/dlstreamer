#!/bin/bash
# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
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

INPUT=${1:-https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4}
DEVICE=${2:-GPU}
OUTPUT=${3:-fps} # Supported values: display, fps

SCRIPTDIR="$(dirname "$(realpath "$0")")"
PYTHON_SCRIPT=$SCRIPTDIR/postproc_callbacks/simple_frame_saver.py

if [[ $OUTPUT == "display" ]] || [[ -z $OUTPUT ]]; then
  SINK_ELEMENT="gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "fps" ]]; then
  SINK_ELEMENT="gvafpscounter ! fakesink async=false "
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

DETECT_MODEL_PATH=${MODELS_PATH}/intel/face-detection-adas-0001/FP32/face-detection-adas-0001.xml

echo Running sample with the following parameters:
echo GST_PLUGIN_PATH="${GST_PLUGIN_PATH}"

read -r PIPELINE << EOM
gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3 ! gvadetect model=$DETECT_MODEL_PATH device=$DEVICE ! queue ! gvapython module=$PYTHON_SCRIPT class=FrameSaver function=process_frame ! $SINK_ELEMENT 
EOM

echo "${PIPELINE}"
PYTHONPATH=$PYTHONPATH:$(dirname "$0")/../../../../python \
$PIPELINE
