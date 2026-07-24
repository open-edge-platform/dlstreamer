#!/bin/bash
# ==============================================================================
# Copyright (C) 2018-2024 Intel Corporation
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
OUTPUT=${2:-display} # Output type, valid values: display, json ,display-and-json 

PROC_PATH() {
    echo ./model_proc/"$1".json
}

PATH_D=${MODELS_PATH}/public/centerface/FP16/centerface.xml
PATH_C1=${MODELS_PATH}/public/dima806_facial_age_image_detection/FP32/dima806_facial_age_image_detection.xml
PATH_C2=${MODELS_PATH}/public/dima806_fairface_gender_image_detection/FP32/dima806_fairface_gender_image_detection.xml
PATH_C3=${MODELS_PATH}/public/dima806_face_emotions_image_detection/FP32/dima806_face_emotions_image_detection.xml

echo Running sample with the following parameters:
echo GST_PLUGIN_PATH="${GST_PLUGIN_PATH}"

PYTHONPATH=$PYTHONPATH:$(dirname "$0")/../../../python \
python3 "$(dirname "$0")"/draw_face_attributes.py -i "${INPUT}" -d "${PATH_D}" -c1 "${PATH_C1}" -c2 "${PATH_C2}" -c3 "${PATH_C3}" -o "${OUTPUT}"
