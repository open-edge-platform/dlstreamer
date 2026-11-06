#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_SUB_STREAM_FRAMES=140
DEFAULT_FRAME_BUDGET=8

rel_path() {
    realpath -m --relative-to=. -- "$1"
}

show_usage() {
    echo ""
    echo "Usage: $0 --input <video.mp4> [--model <model.xml>] [--output <directory>]" \
         "[--sub-stream-frames N] [--frame-budget k]"
    echo ""
    echo "Options:"
    echo ""
    echo "  --input <video.mp4>    Input video (required)"
    echo "  --model <model.xml>    OpenVINO detection model (required)"
    echo "  --output <directory>   Output directory (required)"
    echo "  --sub-stream-frames N  Frames per selection block"
    echo "  --frame-budget k       Selected frames per block"
    echo "  -h, --help             Show this help"
    echo ""
    echo "Example: $0 --input /path/to/video.mp4 --model /path/to/model.xml --output /path/to/output --frame-budget 8"
    echo ""
}

show_help_hint() {
    echo "Run '$0 --help' to see the available options." >&2
}

show_welcome() {
    echo "=============================================================="
    echo " Welcome to the gvaframeselect sample"
    echo "=============================================================="
    echo "This sample detects people in a video with gvadetect and uses"
    echo "gvaframeselect to pick evenly spaced frames from each block of"
    echo "frames and compute a shared ROI."
    echo ""
}

is_uint_in_range() {
    local value=$1
    local max=$2
    local normalized

    [[ "$value" =~ ^[0-9]+$ ]] || return 1
    normalized=${value#"${value%%[!0]*}"}
    [[ -n "$normalized" ]] || normalized=0
    ((${#normalized} <= ${#max})) || return 1
    ((10#$normalized >= 1 && 10#$normalized <= 10#$max))
}

INPUT_VIDEO=""
DETECTION_MODEL=""
OUTPUT_DIR=""
SUB_STREAM_FRAMES=$DEFAULT_SUB_STREAM_FRAMES
FRAME_BUDGET=$DEFAULT_FRAME_BUDGET

while (($# > 0)); do
    case "$1" in
    --input|--model|--output|--sub-stream-frames|--frame-budget)
        option=$1
        if (($# < 2)); then
            echo "Error: missing value for $option" >&2
            show_usage >&2
            exit 2
        fi
        value=$2
        case "$option" in
        --input) INPUT_VIDEO=$value ;;
        --model) DETECTION_MODEL=$value ;;
        --output) OUTPUT_DIR=$value ;;
        --sub-stream-frames) SUB_STREAM_FRAMES=$value ;;
        --frame-budget) FRAME_BUDGET=$value ;;
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

if ! is_uint_in_range "$SUB_STREAM_FRAMES" 4294967295; then
    echo "Error: --sub-stream-frames must be an integer between 1 and 4294967295" >&2
    show_help_hint
    exit 2
fi
if ! is_uint_in_range "$FRAME_BUDGET" 4294967295; then
    echo "Error: --frame-budget must be an integer between 1 and 4294967295" >&2
    show_help_hint
    exit 2
fi

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

if ! mkdir -p "$OUTPUT_DIR/selected_frames"; then
    echo "Error: could not create output directory: $OUTPUT_DIR/selected_frames" >&2
    exit 1
fi
if [[ ! -d "$OUTPUT_DIR/selected_frames" || ! -w "$OUTPUT_DIR/selected_frames" ]]; then
    echo "Error: output directory is not writable: $OUTPUT_DIR/selected_frames" >&2
    exit 1
fi

# Errors above show the values as given; the output below shows paths relative to the current directory.
INPUT_VIDEO=$(rel_path "$INPUT_VIDEO")
DETECTION_MODEL=$(rel_path "$DETECTION_MODEL")
OUTPUT_DIR=$(rel_path "$OUTPUT_DIR")

echo "=============================================================="
echo " Configuration"
echo "=============================================================="
echo "  Input video:       $INPUT_VIDEO"
echo "  Detection model:   $DETECTION_MODEL"
echo "  Output:            $OUTPUT_DIR"
echo "  Sub-stream frames: $SUB_STREAM_FRAMES"
echo "  Frame budget:      $FRAME_BUDGET"
echo "=============================================================="

PIPELINE="gst-launch-1.0 -e \
    filesrc $(printf '%q' "location=$INPUT_VIDEO") ! decodebin3 \"caps=video/x-raw(ANY)\" ! videoconvert ! \
    gvadetect $(printf '%q' "model=$DETECTION_MODEL") device=CPU ! queue ! \
    gvaframeselect \
        sub-stream-frames=$SUB_STREAM_FRAMES \
        frame-budget=$FRAME_BUDGET \
        roi-padding=16 \
        $(printf '%q' "output-dir=$OUTPUT_DIR/selected_frames") ! \
    fakesink sync=false"

echo ""
echo "Starting gvaframeselect pipeline..."
echo "Pipeline: $PIPELINE"
echo ""
eval "$PIPELINE"

echo ""
echo "=============================================================="
echo " Done"
echo "=============================================================="
echo "  Selected frames: $OUTPUT_DIR/selected_frames/"
echo "=============================================================="
    