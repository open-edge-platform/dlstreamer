#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# Deep SORT multi-object tracking sample.
# Uses YOLO for person detection, mars-small128 for re-ID feature extraction,
# and gvatrack with Deep SORT algorithm for identity-preserving tracking.
#
# This sample uses a video file by Sururi Ballıdağ via Pexels
# (https://www.pexels.com)
# ==============================================================================

set -euo pipefail

ALLOWED_DEVICES=("CPU" "GPU" "NPU")
ALLOWED_OUTPUTS=("display" "file" "fps" "json")

# Default values
DET_MODEL=""
REID_MODEL=""
DEVICE="GPU"
INPUT="https://videos.pexels.com/video-files/18552655/18552655-hd_1280_720_30fps.mp4"
OUTPUT="file"
OUTPUT_FILE="deepsort_output.mp4"
DEEPSORT_CFG="max_age=60,max_cosine_distance=0.2,nn_budget=0,object_class=person,reid_max_age=30"

show_usage() {
    echo "Usage: $0 [--det-model DET_MODEL] [--reid-model REID_MODEL] [--device DEVICE] [--input INPUT] [--output OUTPUT] [--deepsort-cfg DEEPSORT_CFG]"
    echo ""
    echo "Arguments:"
    echo "  --det-model DET_MODEL     - Detection model path, YOLO (required)"
    echo "  --reid-model REID_MODEL   - Re-ID model path, mars-small128 (required)"
    echo "  --device DEVICE           - Device to use (default: CPU). Allowed: ${ALLOWED_DEVICES[*]}"
    echo "  --input INPUT             - Input video file or URL (default: Pexels video URL)"
    echo "  --output OUTPUT           - Output type (default: file). Allowed: ${ALLOWED_OUTPUTS[*]}"
    echo "  --output-file FILE        - Output file path/name when --output=file (default: deepsort_output.mp4)"
    echo "  --deepsort-cfg CFG        - Deep SORT tracker config, comma-separated key=value pairs"
    echo "                              (default: max_age=60,max_cosine_distance=0.2,nn_budget=0,object_class=person,reid_max_age=30)"
    echo ""
    echo "Deep SORT config parameters:"
    echo "  max_age              - Max frames a track survives without detection (default: 60)"
    echo "  max_cosine_distance  - Appearance matching threshold (default: 0.2)"
    echo "  nn_budget            - Max features per track, 0=unlimited (default: 0)"
    echo "  object_class         - Only track this detection label (default: person)"
    echo "  reid_max_age         - Frames to keep re-ID gallery after track deletion (default: 30)"
    echo ""
    echo "  --help                    - Show this help message"
    echo ""
}

# Function to check if an item is in an array
containsElement () {
  local element match="$1"
  shift
  for element; do
    [[ "$element" == "$match" ]] && return 0
  done
  return 1
}

# Parse arguments
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --det-model)
            DET_MODEL="$2"
            shift
            ;;
        --reid-model)
            REID_MODEL="$2"
            shift
            ;;
        --device)
            DEVICE="$2"
            if ! containsElement "$DEVICE" "${ALLOWED_DEVICES[@]}"; then
                echo "Invalid device choice. Allowed choices are: ${ALLOWED_DEVICES[*]}"
                exit 1
            fi
            shift
            ;;
        --input)
            INPUT="$2"
            shift
            ;;
        --output)
            OUTPUT="$2"
            if ! containsElement "$OUTPUT" "${ALLOWED_OUTPUTS[@]}"; then
                echo "Invalid output choice. Allowed choices are: ${ALLOWED_OUTPUTS[*]}"
                exit 1
            fi
            shift
            ;;
        --output-file)
            OUTPUT_FILE="$2"
            shift
            ;;
        --deepsort-cfg)
            DEEPSORT_CFG="$2"
            shift
            ;;
        --help)
            show_usage
            exit 0
            ;;
        *)
            echo "Unknown parameter passed: $1"
            exit 1
            ;;
    esac
    shift
done

# Validate required model paths
if [ -z "$DET_MODEL" ]; then
    echo "ERROR - Detection model path is required. Use --det-model." >&2
    exit 1
fi
if [ ! -f "$DET_MODEL" ]; then
    echo "ERROR - Detection model not found: $DET_MODEL" >&2
    exit 1
fi
if [ -z "$REID_MODEL" ]; then
    echo "ERROR - Re-ID model path is required. Use --reid-model." >&2
    exit 1
fi
if [ ! -f "$REID_MODEL" ]; then
    echo "ERROR - Re-ID model not found: $REID_MODEL" >&2
    exit 1
fi

# Build sink element based on OUTPUT type
case "$OUTPUT" in
  display)
    SINK="gvawatermark displ-cfg=show-roi=person,font-scale=0.8 ! gvafpscounter ! autovideosink sync=false"
    ;;
  file)
    OUTFILE="${OUTPUT_FILE}"
    rm -f "$OUTFILE"
    # Find available VA-API H.264 encoder
    if gst-inspect-1.0 va 2>/dev/null | grep -q vah264enc; then
      ENCODER="vah264enc"
    elif gst-inspect-1.0 va 2>/dev/null | grep -q vah264lpenc; then
      ENCODER="vah264lpenc"
    else
      ENCODER="x264enc"
    fi
    SINK="gvawatermark displ-cfg=show-roi=person,font-scale=0.8 ! gvafpscounter ! videoconvert ! ${ENCODER} ! h264parse ! mp4mux ! filesink location=${OUTFILE}"
    echo "Output file: $OUTFILE"
    ;;
  fps)
    SINK="gvafpscounter ! fakesink async=false"
    ;;
  json)
    JSONFILE="deepsort_output.json"
    rm -f "$JSONFILE"
    SINK="gvametaconvert ! gvametapublish method=file file-path=${JSONFILE} file-format=json-lines ! fakesink async=false"
    echo "Output JSON: $JSONFILE"
    ;;
  *)
    echo "Error: Invalid OUTPUT type: $OUTPUT. Supported: display, file, fps, json" >&2
    exit 1
    ;;
esac

if [[ "$INPUT" == *"://"* ]]; then
  SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${INPUT}"
else
  SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

case "$DEVICE" in
  "CPU") PREPROC_BACKEND="opencv" ;;
  "GPU") PREPROC_BACKEND="va-surface-sharing" ;;
  "NPU") PREPROC_BACKEND="va" ;;
  *) echo "Error: Unsupported DEVICE value: $DEVICE"
     exit 1 ;;
esac

PIPELINE="gst-launch-1.0 \
  ${SOURCE_ELEMENT} ! decodebin3 ! \
  gvadetect model=${DET_MODEL} device=${DEVICE} ! queue ! \
  gvainference model=${REID_MODEL} device=${DEVICE} inference-region=roi-list object-class=person ! \
  gvatrack tracking-type=deep-sort deepsort-trck-cfg=\"${DEEPSORT_CFG}\" ! queue ! \
  ${SINK}"

echo ""
echo "Pipeline:"
echo "$PIPELINE"
echo ""

eval "$PIPELINE"

