#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# Pose Estimation Compose — 4-stream pose estimation with different YOLO models,
# composited into a single output video.
#
# This sample uses a video file from https://www.pexels.com/videos/
# ==============================================================================

set -euo pipefail

INPUT=${1:-"videos/pedestrians.mp4"}
DEVICE=${2:-"GPU"}
OUTPUT=${3:-"results/pose_estimation_compose.mp4"}

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MODELS_DIR="${SCRIPT_DIR}/models"

# Model paths and display names
declare -a MODEL_NAMES=("yolo26n-pose" "yolo11n-pose" "yolov8n-pose" "yolov8l-pose")
declare -a MODEL_LABELS=("YOLO26n" "YOLO11n" "YOLOv8n" "YOLOv8l")

# Resolve model XML paths
declare -a MODEL_PATHS=()
for model_name in "${MODEL_NAMES[@]}"; do
    model_dir="${MODELS_DIR}/${model_name}_openvino"
    xml_file=$(find "$model_dir" -name "*.xml" -print -quit 2>/dev/null)
    if [ -z "$xml_file" ]; then
        echo "Error: Model not found in ${model_dir}. Run 'python3 export_models.py' first." >&2
        exit 1
    fi
    MODEL_PATHS+=("$xml_file")
done

# Resolve input path
if [[ "$INPUT" != /* ]]; then
    INPUT="${SCRIPT_DIR}/${INPUT}"
fi
if [ ! -f "$INPUT" ]; then
    echo "Error: Input file not found: ${INPUT}" >&2
    exit 1
fi

# Create output directory
OUTPUT_DIR="$(dirname "$OUTPUT")"
if [[ "$OUTPUT" != /* ]]; then
    OUTPUT="${SCRIPT_DIR}/${OUTPUT}"
    OUTPUT_DIR="$(dirname "$OUTPUT")"
fi
mkdir -p "$OUTPUT_DIR"
rm -f "$OUTPUT"

# Detect H.264 encoder
if gst-inspect-1.0 va 2>/dev/null | grep -q vah264enc; then
    ENCODER="vah264enc"
elif gst-inspect-1.0 va 2>/dev/null | grep -q vah264lpenc; then
    ENCODER="vah264lpenc"
else
    echo "Error: VA-API H.264 encoder not found." >&2
    exit 1
fi

# Get input video resolution for tile sizing
# Each tile is half the original resolution
TILE_W=683
TILE_H=360

# Build the vacompositor sink with 2x2 grid layout
# Stream 0: top-left, Stream 1: top-right, Stream 2: bottom-left, Stream 3: bottom-right
COMPOSITOR="vacompositor name=comp \
  sink_0::xpos=0 sink_0::ypos=0 \
  sink_1::xpos=${TILE_W} sink_1::ypos=0 \
  sink_2::xpos=0 sink_2::ypos=${TILE_H} \
  sink_3::xpos=${TILE_W} sink_3::ypos=${TILE_H}"

# Build the output pipeline
COMP_OUTPUT="${COMPOSITOR} ! \
${ENCODER} ! h264parse ! mp4mux fragment-duration=1000 ! \
filesink location=${OUTPUT}"

# Build per-stream pipelines
STREAMS=""
for i in 0 1 2 3; do
    STREAM="filesrc location=${INPUT} ! decodebin3 caps=\"video/x-raw(ANY)\" ! \
gvadetect model=${MODEL_PATHS[$i]} device=${DEVICE} \
model-instance-id=pose_${i} batch-size=1 \
scheduling-policy=latency ! \
queue flush-on-eos=true ! \
gvawatermark displ-cfg=show-labels=false,font-scale=1.0,ff-custom-txt=${MODEL_LABELS[$i]} ! \
vapostproc ! video/x-raw(memory:VAMemory),width=${TILE_W},height=${TILE_H} ! \
queue ! comp.sink_${i} "
    STREAMS="${STREAMS} ${STREAM}"
done

PIPELINE="gst-launch-1.0 ${COMP_OUTPUT} ${STREAMS}"

echo "=== Pose Estimation Compose ==="
echo "Input:  ${INPUT}"
echo "Output: ${OUTPUT}"
echo "Device: ${DEVICE}"
echo "Models: ${MODEL_LABELS[*]}"
echo ""
echo "${PIPELINE}"
echo ""

${PIPELINE}

echo ""
echo "=== Pipeline complete ==="
echo "Output saved to: ${OUTPUT}"
