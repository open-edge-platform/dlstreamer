#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

# Configuration parameters
MODELS_PATH=${MODELS_PATH:-.}  # Path to models directory (e.g., /path/to/omz_models)
INPUT=${1:-https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4}  # Video file or URL
# NOTE: To use a local video file, provide the path in following format: file:///path/to/video.mp4
MODEL=${2:-${MODELS_PATH}/public/yolo11n/FP16/yolo11n.xml}  # Detection model (YOLO, SSD, etc.)
OUTPUT=${3:-/tmp/vehicle_counter_output.mp4}  # Output video file (H.264 MP4)
DEVICE=${4:-GPU}  # Inference device: GPU (default), CPU, or NPU
JSON_METADATA=${5:-}  # Optional output file for analytics metadata (JSON Lines format)

echo "Running vehicle_counter sample"
echo "  Input:    ${INPUT}"
echo "  Model:    ${MODEL}"
echo "  Output:   ${OUTPUT}"
echo "  Device:   ${DEVICE}"
[ -n "${JSON_METADATA}" ] && echo "  Metadata: ${JSON_METADATA} (JSON Lines format with detections and crossing events)"

# Validate model file exists
if [ ! -f "${MODEL}" ]; then
    echo "Error: Model file not found: ${MODEL}" >&2
    echo "Hint: Set MODELS_PATH environment variable to directory containing models" >&2
    echo "      Example: export MODELS_PATH=/path/to/models" >&2
    exit 1
fi

python3 "$(dirname "$0")"/vehicle_counter.py "${INPUT}" "${MODEL}" "${OUTPUT}" "${DEVICE}" ${JSON_METADATA:+"${JSON_METADATA}"}

echo "Done. Output written to: ${OUTPUT}"
