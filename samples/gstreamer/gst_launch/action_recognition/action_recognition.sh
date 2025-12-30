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
  echo "  INPUT   - Input source (default: Pexels video URL)"
  echo "  DEVICE  - Device (default: CPU). Supported: CPU, GPU, NPU"
  echo "  OUTPUT  - Output type (default: file). Supported: file, display, fps, json, display-and-json"
  echo ""
  exit 0
fi

INPUT=${1:-https://videos.pexels.com/video-files/5144823/5144823-uhd_3840_2160_25fps.mp4}
DEVICE=${2:-CPU}
OUTPUT=${3:-file} # Supported values: display, fps, json, display-and-json, file

MODEL_ENCODER=${MODELS_PATH}/intel/action-recognition-0001/action-recognition-0001-encoder/FP32/action-recognition-0001-encoder.xml
MODEL_DECODER=${MODELS_PATH}/intel/action-recognition-0001/action-recognition-0001-decoder/FP32/action-recognition-0001-decoder.xml

if [[ -z $INPUT ]]; then
  echo Error: Input video path is empty
  echo Please provide path to input video file
  exit
fi

if [[ $OUTPUT == "display" ]]; then
  SINK_ELEMENT="vapostproc ! gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "fps" ]]; then
  SINK_ELEMENT=" gvafpscounter ! fakesink async=false"
elif [[ $OUTPUT == "json" ]]; then
  rm -f output.json
  SINK_ELEMENT=" gvametaconvert ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false"
elif [[ $OUTPUT == "display-and-json" ]]; then
  rm -f output.json
  SINK_ELEMENT="vapostproc ! gvawatermark ! gvametaconvert ! gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "file" ]]; then
  FILE="$(basename ${INPUT%.*})"
  rm -f "action_recognition_${FILE}_${DEVICE}.mp4"
  if [[ $(gst-inspect-1.0 va | grep vah264enc) ]]; then
    ENCODER="vah264enc"
  elif [[ $(gst-inspect-1.0 va | grep vah264lpenc) ]]; then
    ENCODER="vah264lpenc"
  else
    echo "Error - VA-API H.264 encoder not found."
    exit
  fi
  SINK_ELEMENT="vapostproc ! gvawatermark ! gvafpscounter ! ${ENCODER} ! h264parse ! mp4mux ! filesink location=action_recognition_${FILE}_${DEVICE}.mp4"
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

DIR=$(dirname "$0")

PIPELINE="gst-launch-1.0 \
$SOURCE_ELEMENT ! \
decodebin3 ! \
video_inference \
  process=' \
    openvino_tensor_inference model=$MODEL_ENCODER device=$DEVICE ! \
    tensor_sliding_window ! \
    openvino_tensor_inference model=$MODEL_DECODER device=$DEVICE' \
  postprocess=' \
    tensor_postproc_label labels-file=$DIR/kinetics_400.txt method=softmax' ! \
$SINK_ELEMENT"

echo "${PIPELINE}"
eval "$PIPELINE"
