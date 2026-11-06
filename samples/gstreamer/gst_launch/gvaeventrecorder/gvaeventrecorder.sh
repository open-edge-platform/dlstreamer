#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ===============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

rel_path() {
    realpath -m --relative-to=. -- "$1"
}

show_usage() {
    echo "Usage: $0 --input <video.mp4> [--model <model.xml>] [--output <directory>]"
    echo "Options:"
    echo "  --input <video.mp4>    Input video (required)"
    echo "  --model <model.xml>    OpenVINO detection model (required)"
    echo "  --output <directory>   Output directory (required)"
    echo "  -h, --help             Show this help"
    echo "Example: $0 --input /path/to/video.mp4 --model /path/to/model.xml --output /path/to/output"
}

show_help_hint() {
    echo "Run '$0 --help' to see the available options." >&2
}

show_welcome() {
    echo "=============================================================="
    echo " Welcome to the gvaeventrecorder sample"
    echo "=============================================================="
    echo "This sample detects people in a video with gvadetect and uses"
    echo "gvaeventrecorder to record event clips around each detection"
    echo "(pre-event, event and post-event frames)."
    echo ""
}

INPUT_VIDEO=""
DETECTION_MODEL=""
OUTPUT_DIR=""

while (($# > 0)); do
    case "$1" in
    --input|--model|--output)
        if (($# < 2)); then
            echo "Error: missing value for $1" >&2
            show_usage >&2
            exit 2
        fi
        case "$1" in
        --input) INPUT_VIDEO=$2 ;;
        --model) DETECTION_MODEL=$2 ;;
        --output) OUTPUT_DIR=$2 ;;
        esac
        shift 2
        ;;
    -h|--help)
        show_usage
        exit 0
        ;;
    *)
        echo "Error: unknown argument: $1" >&2
        show_usage >&2
        exit 2
        ;;
    esac
done

show_welcome

if [[ -z "$INPUT_VIDEO" ]]; then
    echo "Error: --input is required" >&2
    show_usage >&2
    exit 2
fi

if [[ -z "$DETECTION_MODEL" ]]; then
    echo "Error: --model is required" >&2
    show_usage >&2
    exit 2
fi

if [[ -z "$OUTPUT_DIR" ]]; then
    echo "Error: --output is required" >&2
    show_usage >&2
    exit 2
fi

if [[ ! -r "$INPUT_VIDEO" ]]; then
    echo "Error: input video is not readable: $INPUT_VIDEO" >&2
    show_help_hint
    exit 1
fi

if [[ ! -r "$DETECTION_MODEL" ]]; then
    echo "Error: detection model is not readable: $DETECTION_MODEL" >&2
    show_help_hint
    exit 1
fi

if ! command -v gst-launch-1.0 >/dev/null 2>&1; then
    echo "Error: gst-launch-1.0 was not found; configure the DL Streamer environment first" >&2
    exit 1
fi

OUTPUT_DIRECTORIES=(
    "$OUTPUT_DIR/event-clips"
    "$OUTPUT_DIR/event-json"
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

# Errors above show the values as given; the output below shows paths relative to the current directory.
INPUT_VIDEO=$(rel_path "$INPUT_VIDEO")
DETECTION_MODEL=$(rel_path "$DETECTION_MODEL")
OUTPUT_DIR=$(rel_path "$OUTPUT_DIR")

echo "=============================================================="
echo " Configuration"
echo "=============================================================="
echo "  Input video:     $INPUT_VIDEO"
echo "  Detection model: $DETECTION_MODEL"
echo "  Output:          $OUTPUT_DIR"
echo "=============================================================="

PIPELINE="gst-launch-1.0 -e \
    filesrc $(printf '%q' "location=$INPUT_VIDEO") ! decodebin3 \"caps=video/x-raw(ANY)\" ! videoconvert ! \
    gvadetect $(printf '%q' "model=$DETECTION_MODEL") device=CPU ! queue ! \
    gvaeventrecorder \
        target-classes=person \
        confidence-threshold=0.5 \
        pre-event-frames=30 \
        post-event-frames=30 \
        event-gap-frames=3 \
        $(printf '%q' "event-clip-output-dir=$OUTPUT_DIR/event-clips") ! \
    gvametaconvert format=json ! \
    gvametapublish file-format=2 $(printf '%q' "file-path=$OUTPUT_DIR/event-json/events.json") ! \
    fakesink sync=false"

echo ""
echo "Starting gvaeventrecorder pipeline..."
echo "Pipeline: $PIPELINE"
echo ""
eval "$PIPELINE"

echo ""
echo "=============================================================="
echo " Done"
echo "=============================================================="
echo "  Event clips:    $OUTPUT_DIR/event-clips/"
echo "  Event metadata: $OUTPUT_DIR/event-json/events.json"
echo "=============================================================="
