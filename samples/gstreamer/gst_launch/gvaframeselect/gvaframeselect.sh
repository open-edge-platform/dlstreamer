#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

show_usage() {
    echo "Usage: $0 [options] <input-video> <detection-model.xml> <output-directory>"
    echo "Options:"
    echo "  --sub-stream-frames N  Frames per selection block (default: 900)"
    echo "  --frame-budget k       Selected frames per block (default: 8)"
    echo "  -h, --help             Show this help"
    echo "Example: $0 --sub-stream-frames 900 --frame-budget 8 /path/to/video.mp4 /path/to/model.xml /path/to/output"
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

SUB_STREAM_FRAMES=900
FRAME_BUDGET=8
POSITIONAL_ARGS=()

while (($# > 0)); do
    case "$1" in
    --sub-stream-frames|--frame-budget)
        option=$1
        if (($# < 2)); then
            echo "Error: missing value for $option" >&2
            show_usage >&2
            exit 2
        fi
        value=$2
        case "$option" in
        --sub-stream-frames) SUB_STREAM_FRAMES=$value ;;
        --frame-budget) FRAME_BUDGET=$value ;;
        esac
        shift 2
        ;;
    -h|--help)
        show_usage
        exit 0
        ;;
    -* )
        echo "Error: unknown option: $1" >&2
        show_usage >&2
        exit 2
        ;;
    *)
        POSITIONAL_ARGS+=("$1")
        shift
        ;;
    esac
done

if ((${#POSITIONAL_ARGS[@]} != 3)); then
    show_usage >&2
    exit 1
fi

if ! is_uint_in_range "$SUB_STREAM_FRAMES" 4294967295; then
    echo "Error: --sub-stream-frames must be an integer between 1 and 4294967295" >&2
    exit 2
fi
if ! is_uint_in_range "$FRAME_BUDGET" 4294967295; then
    echo "Error: --frame-budget must be an integer between 1 and 4294967295" >&2
    exit 2
fi

INPUT_VIDEO=${POSITIONAL_ARGS[0]}
DETECTION_MODEL=${POSITIONAL_ARGS[1]}
OUTPUT_DIR=${POSITIONAL_ARGS[2]}

if [[ ! -r "$INPUT_VIDEO" ]]; then
    echo "Error: input video is not readable: $INPUT_VIDEO" >&2
    exit 1
fi

if [[ ! -r "$DETECTION_MODEL" ]]; then
    echo "Error: detection model is not readable: $DETECTION_MODEL" >&2
    exit 1
fi

if [[ -z "$OUTPUT_DIR" ]]; then
    echo "Error: output directory must not be empty" >&2
    exit 1
fi

if ! command -v gst-launch-1.0 >/dev/null 2>&1; then
    echo "Error: gst-launch-1.0 was not found; configure the DL Streamer environment first" >&2
    exit 1
fi

if ! mkdir -p "$OUTPUT_DIR"; then
    echo "Error: could not create output directory: $OUTPUT_DIR" >&2
    exit 1
fi
if [[ ! -d "$OUTPUT_DIR" || ! -w "$OUTPUT_DIR" ]]; then
    echo "Error: output directory is not writable: $OUTPUT_DIR" >&2
    exit 1
fi

echo "=== gvaframeselect sample configuration ==="
echo "Input video: $INPUT_VIDEO"
echo "Detection model: $DETECTION_MODEL"
echo "Output directory: $OUTPUT_DIR"
echo "Sub-stream frames: $SUB_STREAM_FRAMES"
echo "Frame budget: $FRAME_BUDGET"
echo "==========================================="

gst-launch-1.0 -e \
    filesrc "location=$INPUT_VIDEO" ! decodebin3 ! videoconvert ! \
    gvadetect "model=$DETECTION_MODEL" device=CPU ! queue ! \
    gvaframeselect "sub-stream-frames=$SUB_STREAM_FRAMES" "frame-budget=$FRAME_BUDGET" \
        "output-dir=$OUTPUT_DIR" ! \
    fakesink sync=false