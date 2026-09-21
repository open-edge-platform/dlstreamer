#!/bin/bash
# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
set -e

INPUT=${1:-how_are_you_doing.wav}
OUTPUT=${2:-stdout}

MODEL=aclnet

if [ -z "$AUDIO_MODELS_PATH" ]
then
  AUDIO_MODELS_PATH="${MODELS_PATH:=.}"
fi
if [ ! -d "$AUDIO_MODELS_PATH" ]
then
  echo -e "\e[31mERROR: Invalid model directory $AUDIO_MODELS_PATH. Prepare required models via scripts/download_models (see scripts/download_models/README.md).\e[0m" 1>&2
  exit 1
fi

if [[ $INPUT == *"://"* ]]; then
  SOURCE_ELEMENT="urisourcebin uri=${INPUT}"
else
  SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

PROC_PATH() {
    AUDIO_MODEL_PROC_PATH=$(dirname "$0")/model_proc/$1.json
    if [ ! -f "$AUDIO_MODEL_PROC_PATH" ]
    then
      echo -e "\e[31mERROR: Invalid model-proc file path $AUDIO_MODEL_PROC_PATH\e[0m" 1>&2
      exit 1
    fi
    echo "$AUDIO_MODEL_PROC_PATH"
}

MODEL_PATH=${MODELS_PATH}/public/aclnet/FP32/aclnet.xml

MODEL_PROC_PATH=$(PROC_PATH $MODEL)

PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT ! \
decodebin3 ! audioresample ! audioconvert ! audio/x-raw, channels=1,format=S16LE,rate=16000 ! audiomixer output-buffer-duration=100000000 ! \
gvaaudiodetect model=$MODEL_PATH model-proc=$MODEL_PROC_PATH sliding-window=0.2 \
! gvametaconvert ! gvametapublish file-path=$OUTPUT file-format=json-lines ! \
fakesink"

echo "${PIPELINE}"
${PIPELINE}
