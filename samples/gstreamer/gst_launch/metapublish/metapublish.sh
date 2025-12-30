#!/bin/bash
# ==============================================================================
# Copyright (C) 2018-2025 Intel Corporation
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
  echo "Usage: $0 [INPUT] [METHOD] [OUTPUT] [FORMAT] [TOPIC]"
  echo ""
  echo "Arguments:"
  echo "  INPUT     - Input source (default: Pexels video URL)"
  echo "  METHOD    - Metapublish method (default: file). Supported: file, kafka, mqtt"
  echo "  OUTPUT    - Output destination (default: stdout for file, localhost:9092 for kafka, localhost:1883 for mqtt)"
  echo "  FORMAT    - Output format (default: json for file, json-lines for kafka and mqtt). Supported: json, json-lines"
  echo "  TOPIC     - Topic name (default: dlstreamer). Required for kafka and mqtt"
  echo ""
  exit 0
fi

INPUT=${1:-https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4}
METHOD=${2:-file} # Accepts: file, kafka, mqtt
OUTPUT=${3} # Path to file if method==file, host and port of message broker if method==kafka/mqtt. Default: "stdout" for file, "localhost:9092" for kafka, and "localhost:1883" for mqtt
FORMAT=${4} # json or json-lines
TOPIC=${5:-dlstreamer} # Required for kafka and mqtt

MODEL1=face-detection-adas-0001
MODEL2=age-gender-recognition-retail-0013
PRECISION=FP32
DEVICE=CPU

OUTPUT_PROPERTY=""
if [[ "$METHOD" == "file" ]]; then
    if [ "${OUTPUT}" != "" ]; then # default output file is stdout - print to console
        OUTPUT_PROPERTY="file-path=${OUTPUT}"
        rm -f "${OUTPUT}" # TODO - is removing file needed?
    fi
    if [ "${FORMAT}" == "" ]; then
        FORMAT="json"
    fi
else # kafka, mqtt
    if [ "${OUTPUT}" == "" ]; then
        if [[ "$METHOD" == "mqtt" ]]; then
             OUTPUT="localhost:1883"
        elif [[ "$METHOD" == "kafka" ]]; then
             OUTPUT="localhost:9092"
        fi
    fi
    if [ "${FORMAT}" == "" ]; then
        FORMAT="json-lines"
    fi
    OUTPUT_PROPERTY="address=$OUTPUT topic=$TOPIC"
fi

if [[ "$FORMAT" == "json-lines" ]]; then
    JSON_INDENT=-1 # no indent
else # "json"
    JSON_INDENT=4 # Number of spaces to indent pretty json
fi

if [[ $INPUT == "/dev/video"* ]]; then
  SOURCE_ELEMENT="v4l2src device=${INPUT}"
elif [[ $INPUT == *"://"* ]]; then
  SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${INPUT}"
else
  SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

MODEL1_PATH="${MODELS_PATH:=.}"/intel/$MODEL1/$PRECISION/$MODEL1.xml
MODEL2_PATH="${MODELS_PATH:=.}"/intel/$MODEL2/$PRECISION/$MODEL2.xml
MODEL2_PROC="$(dirname "$0")"/model_proc/$MODEL2.json

PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT ! \
decodebin3 ! \
gvadetect model=$MODEL1_PATH device=$DEVICE ! queue ! \
gvaclassify model=$MODEL2_PATH model-proc=$MODEL2_PROC device=$DEVICE ! queue ! \
gvametaconvert json-indent=$JSON_INDENT ! \
gvametapublish method=$METHOD file-format=$FORMAT $OUTPUT_PROPERTY ! \
fakesink sync=false"

echo "${PIPELINE}"
#LAUNCH
${PIPELINE}
