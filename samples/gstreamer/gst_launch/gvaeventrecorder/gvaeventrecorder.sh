#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ===============================================================================
set -euo pipefail

show_usage() {
    echo "Usage: $0 <input-video> <detection-model.xml> <output-directory>"
    echo "Example: $0 input.mp4 model.xml output"
}

if [[ $# -eq 1 && "$1" == "--help" ]]; then
    show_usage
    exit 0
fi

if [[ $# -ne 3 ]]; then
    show_usage >&2
    exit 1
fi

INPUT_VIDEO=$1
DETECTION_MODEL=$2
OUTPUT_DIR=$3

if [[ -z "$OUTPUT_DIR" ]]; then
    echo "Error: output directory must not be empty" >&2
    exit 1
fi

if [[ ! -r "$INPUT_VIDEO" ]]; then
    echo "Error: input video is not readable: $INPUT_VIDEO" >&2
    exit 1
fi

if [[ ! -r "$DETECTION_MODEL" ]]; then
    echo "Error: detection model is not readable: $DETECTION_MODEL" >&2
    exit 1
fi

if ! command -v gst-launch-1.0 >/dev/null 2>&1; then
    echo "Error: gst-launch-1.0 was not found; configure the DL Streamer environment first" >&2
    exit 1
fi

OUTPUT_DIRECTORIES=(
    "$OUTPUT_DIR/full-clip"
    "$OUTPUT_DIR/downstream"
    "$OUTPUT_DIR/events"
)

if ! mkdir -p "${OUTPUT_DIRECTORIES[@]}"; then
    echo "Error: could not create output directories under: $OUTPUT_DIR" >&2
    exit 1
fi

for output_directory in "${OUTPUT_DIRECTORIES[@]}"; do
    if [[ ! -d "$output_directory" || ! -w "$output_directory" ]]; then
        echo "Error: output directory is not writable: $output_directory" >&2
        exit 1
    fi
done

echo "=== gvaeventrecorder sample configuration ==="
echo "Input video: $INPUT_VIDEO"
echo "Detection model: $DETECTION_MODEL"
echo "Output directory: $OUTPUT_DIR"
echo "============================================="

PIPELINE="gst-launch-1.0 -e \
    filesrc $(printf '%q' "location=$INPUT_VIDEO") ! decodebin3 ! videoconvert ! \
    gvadetect $(printf '%q' "model=$DETECTION_MODEL") device=CPU ! queue ! \
    gvaeventrecorder \
        target-classes=person \
        confidence-threshold=0.5 \
        pre-event-frames=30 \
        post-event-frames=30 \
        event-gap-frames=3 \
        $(printf '%q' "full-clip-output-dir=$OUTPUT_DIR/full-clip") \
        $(printf '%q' "downstream-output-dir=$OUTPUT_DIR/downstream") ! \
    gvametaconvert format=json ! \
    gvametapublish file-format=2 $(printf '%q' "file-path=$OUTPUT_DIR/events/events.json") ! \
    fakesink sync=false"

echo ""
echo "Starting gvaeventrecorder pipeline..."
echo "Pipeline: $PIPELINE"
echo ""
eval "$PIPELINE"

echo ""
echo "gvaeventrecorder pipeline completed."
echo "Event metadata saved to: $OUTPUT_DIR/events/events.json"
