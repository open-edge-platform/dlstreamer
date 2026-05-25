#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

MODELS_PATH=${MODELS_PATH:-.}
INPUT=${1:-https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4}
MODEL=${2:-${MODELS_PATH}/public/yolo11n/FP16/yolo11n.xml}
OUTPUT=${3:-/tmp/vehicle_counter_output.mp4}
DEVICE=${4:-GPU}
JSON_METADATA=${5:-}

echo "Running vehicle_counter sample"
echo "  Input:    ${INPUT}"
echo "  Model:    ${MODEL}"
echo "  Output:   ${OUTPUT}"
echo "  Device:   ${DEVICE}"
[ -n "${JSON_METADATA}" ] && echo "  Metadata: ${JSON_METADATA}"

python3 "$(dirname "$0")"/vehicle_counter.py "${INPUT}" "${MODEL}" "${OUTPUT}" "${DEVICE}" ${JSON_METADATA:+"${JSON_METADATA}"}

echo "Done. Output written to: ${OUTPUT}"
