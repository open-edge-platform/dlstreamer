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
    echo "  --frame-budget N       Selected frames per block (default: 8)"
    echo "  --out-width N          Scaled ROI width (default: 448; match the next model's input)"
    echo "  --out-height N         Scaled ROI height (default: 448; match the next model's input)"
    echo "  -h, --help             Show this help"
    echo "Example: $0 --out-width 640 /path/to/video.mp4 /path/to/model.xml /path/to/output"
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
OUT_WIDTH=448
OUT_HEIGHT=448
POSITIONAL_ARGS=()

while (($# > 0)); do
    case "$1" in
    --sub-stream-frames|--frame-budget|--out-width|--out-height)
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
        --out-width) OUT_WIDTH=$value ;;
        --out-height) OUT_HEIGHT=$value ;;
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
if ! is_uint_in_range "$OUT_WIDTH" 8192; then
    echo "Error: --out-width must be an integer between 1 and 8192" >&2
    exit 2
fi
if ! is_uint_in_range "$OUT_HEIGHT" 8192; then
    echo "Error: --out-height must be an integer between 1 and 8192" >&2
    exit 2
fi

INPUT_VIDEO=${POSITIONAL_ARGS[0]}
DETECTION_MODEL=${POSITIONAL_ARGS[1]}
OUTPUT_DIR=${POSITIONAL_ARGS[2]}
SELECTED_FRAMES_DIR="$OUTPUT_DIR/selected_frames"
ROI_OUTPUT_DIR="$OUTPUT_DIR/roi_scaled_frames"

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

mkdir -p "$SELECTED_FRAMES_DIR" "$ROI_OUTPUT_DIR"
for output_directory in "$SELECTED_FRAMES_DIR" "$ROI_OUTPUT_DIR"; do
    if [[ ! -d "$output_directory" || ! -w "$output_directory" ]]; then
        echo "Error: output directory is not writable: $output_directory" >&2
        exit 1
    fi
done

echo "=== gvaroiscale sample configuration ==="
echo "Input video: $INPUT_VIDEO"
echo "Detection model: $DETECTION_MODEL"
echo "Selected frames: $SELECTED_FRAMES_DIR"
echo "Scaled ROI frames: $ROI_OUTPUT_DIR"
echo "Sub-stream frames: $SUB_STREAM_FRAMES"
echo "Frame budget: $FRAME_BUDGET"
echo "Output dimensions: ${OUT_WIDTH}x${OUT_HEIGHT}"
echo "========================================="

gst-launch-1.0 -e \
    filesrc "location=$INPUT_VIDEO" ! decodebin3 "caps=video/x-raw(ANY)" ! videoconvert ! \
    gvadetect "model=$DETECTION_MODEL" device=CPU ! queue ! \
    gvaframeselect "sub-stream-frames=$SUB_STREAM_FRAMES" "frame-budget=$FRAME_BUDGET" \
        "output-dir=$SELECTED_FRAMES_DIR" ! \
    videoconvert ! video/x-raw,format=RGB ! \
    gvaroiscale "out-width=$OUT_WIDTH" "out-height=$OUT_HEIGHT" target-classes=person \
        "output-dir=$ROI_OUTPUT_DIR" ! \
    fakesink sync=false