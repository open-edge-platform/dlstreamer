#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_VIDEO_URL="https://github.com/intel-iot-devkit/sample-videos/raw/master/people-detection.mp4"
DEFAULT_VIDEO="$SCRIPT_DIR/video/people-detection.mp4"
DEFAULT_MODEL="$SCRIPT_DIR/models/public/yolov8s/FP16/yolov8s.xml"
DEFAULT_OUTPUT_DIR="$SCRIPT_DIR/output"
DEFAULT_SUB_STREAM_FRAMES=140
DEFAULT_FRAME_BUDGET=8
DEFAULT_OUT_WIDTH=448
DEFAULT_OUT_HEIGHT=448

rel_path() {
    realpath -m --relative-to=. -- "$1"
}

show_usage() {
    echo ""
    echo "Usage: $0 [--input <video.mp4>] [--model <model.xml>] [--output <directory>]" \
         "[--sub-stream-frames N] [--frame-budget N] [--out-width N] [--out-height N]"
    echo ""
    echo "Options (an omitted option uses its default):"
    echo ""
    echo "  --input <video.mp4>    Input video (default: $(rel_path "$DEFAULT_VIDEO"), downloaded if missing)"
    echo "  --model <model.xml>    OpenVINO detection model (default: $(rel_path "$DEFAULT_MODEL"), exported if missing)"
    echo "  --output <directory>   Output directory (default: $(rel_path "$DEFAULT_OUTPUT_DIR"))"
    echo "  --sub-stream-frames N  Frames per selection block (default: $DEFAULT_SUB_STREAM_FRAMES)"
    echo "  --frame-budget N       Selected frames per block (default: $DEFAULT_FRAME_BUDGET)"
    echo "  --out-width N          Scaled ROI width (default: $DEFAULT_OUT_WIDTH; match the next model's input)"
    echo "  --out-height N         Scaled ROI height (default: $DEFAULT_OUT_HEIGHT; match the next model's input)"
    echo "  -h, --help             Show this help"
    echo ""
    echo "Example: $0 --input /path/to/video.mp4 --model /path/to/model.xml --output /path/to/output --out-width 640"
    echo ""
}

show_help_hint() {
    echo "Run '$0 --help' to see the available options." >&2
}

show_welcome() {
    echo "=============================================================="
    echo " Welcome to the gvaroiscale sample"
    echo "=============================================================="
    echo "This sample detects people in a video with gvadetect, records"
    echo "event clips with gvaeventrecorder, picks evenly spaced frames"
    echo "with gvaframeselect and scales the shared person ROI with"
    echo "gvaroiscale."
    echo ""
}

download_default_video() {
    echo "Downloading default input video: $DEFAULT_VIDEO_URL"
    mkdir -p "$(dirname "$DEFAULT_VIDEO")"
    if ! curl -fL --retry 3 -o "$DEFAULT_VIDEO.part" "$DEFAULT_VIDEO_URL"; then
        rm -f "$DEFAULT_VIDEO.part"
        echo "Error: could not download $DEFAULT_VIDEO_URL" >&2
        exit 1
    fi
    mv "$DEFAULT_VIDEO.part" "$DEFAULT_VIDEO"
}

# Exports yolov8s to OpenVINO IR with the DL Streamer model download script.
download_default_model() {
    local tools_dir="" candidate
    for candidate in "$SCRIPT_DIR/../../../../scripts/download_models" /opt/intel/dlstreamer/scripts/download_models; do
        if [[ -f "$candidate/download_ultralytics_models.py" ]]; then
            tools_dir=$(cd "$candidate" && pwd)
            break
        fi
    done
    if [[ -z "$tools_dir" ]]; then
        echo "Error: scripts/download_models/download_ultralytics_models.py was not found" >&2
        exit 1
    fi

    local venv="$SCRIPT_DIR/.ultralytics_models_venv" created_venv=0
    if ! env -u PYTHONPATH "$venv/bin/python" -c "import ultralytics" 2>/dev/null; then
        created_venv=1
        echo "Python environment not found in $(rel_path "$venv"); creating it for the model export (needs network access, may take a few minutes)..."
        local requirements="$tools_dir/requirements_download_ultralytics_models.txt"
        if command -v uv >/dev/null 2>&1; then
            uv venv --allow-existing "$venv" &&
                uv pip install --python "$venv/bin/python" --index-strategy unsafe-best-match -r "$requirements"
        else
            python3 -m venv "$venv" && "$venv/bin/pip" install -r "$requirements"
        fi || {
            echo "Error: could not prepare Python environment in $(rel_path "$venv")" >&2
            exit 1
        }
    fi

    echo "Exporting yolov8s to $(rel_path "$(dirname "$DEFAULT_MODEL")")"
    # Ultralytics downloads the .pt weights into the working directory; keep them under models/.
    mkdir -p "$SCRIPT_DIR/models"
    if ! (cd "$SCRIPT_DIR/models" && env -u PYTHONPATH "$venv/bin/python" "$tools_dir/download_ultralytics_models.py" \
        --model yolov8s.pt --outdir "$(dirname "$DEFAULT_MODEL")" --half); then
        echo "Error: could not export yolov8s" >&2
        exit 1
    fi

    # Only remove an environment created by this run; a pre-existing one is left alone.
    if ((created_venv)); then
        echo "Removing temporary Python environment $(rel_path "$venv")"
        rm -rf "$venv"
    fi
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

INPUT_VIDEO=$DEFAULT_VIDEO
DETECTION_MODEL=$DEFAULT_MODEL
OUTPUT_DIR=$DEFAULT_OUTPUT_DIR
SUB_STREAM_FRAMES=$DEFAULT_SUB_STREAM_FRAMES
FRAME_BUDGET=$DEFAULT_FRAME_BUDGET
OUT_WIDTH=$DEFAULT_OUT_WIDTH
OUT_HEIGHT=$DEFAULT_OUT_HEIGHT

while (($# > 0)); do
    case "$1" in
    --input|--model|--output|--sub-stream-frames|--frame-budget|--out-width|--out-height)
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
        --out-width) OUT_WIDTH=$value ;;
        --out-height) OUT_HEIGHT=$value ;;
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
if ! is_uint_in_range "$OUT_WIDTH" 8192; then
    echo "Error: --out-width must be an integer between 1 and 8192" >&2
    show_help_hint
    exit 2
fi
if ! is_uint_in_range "$OUT_HEIGHT" 8192; then
    echo "Error: --out-height must be an integer between 1 and 8192" >&2
    show_help_hint
    exit 2
fi

show_welcome

if [[ "$INPUT_VIDEO" == "$DEFAULT_VIDEO" && ! -s "$DEFAULT_VIDEO" ]]; then
    download_default_video
fi

if [[ "$DETECTION_MODEL" == "$DEFAULT_MODEL" && ! -s "$DEFAULT_MODEL" ]]; then
    download_default_model
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

if [[ -z "$OUTPUT_DIR" ]]; then
    echo "Error: output directory must not be empty" >&2
    show_help_hint
    exit 1
fi

if ! command -v gst-launch-1.0 >/dev/null 2>&1; then
    echo "Error: gst-launch-1.0 was not found; configure the DL Streamer environment first" >&2
    exit 1
fi

OUTPUT_DIRECTORIES=(
    "$OUTPUT_DIR/event-clips"
    "$OUTPUT_DIR/selected_frames"
    "$OUTPUT_DIR/roi_scaled_frames"
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
echo "  Input video:       $INPUT_VIDEO"
echo "  Detection model:   $DETECTION_MODEL"
echo "  Output:            $OUTPUT_DIR"
echo "  Sub-stream frames: $SUB_STREAM_FRAMES"
echo "  Frame budget:      $FRAME_BUDGET"
echo "  Output dimensions: ${OUT_WIDTH}x${OUT_HEIGHT}"
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
    gvaframeselect \
        sub-stream-frames=$SUB_STREAM_FRAMES \
        frame-budget=$FRAME_BUDGET \
        roi-padding=16 \
        target-classes=person \
        $(printf '%q' "output-dir=$OUTPUT_DIR/selected_frames") ! \
    videoconvert ! video/x-raw,format=RGB ! \
    gvaroiscale \
        out-width=$OUT_WIDTH \
        out-height=$OUT_HEIGHT \
        target-classes=person \
        $(printf '%q' "output-dir=$OUTPUT_DIR/roi_scaled_frames") ! \
    fakesink sync=false"

echo ""
echo "Starting gvaroiscale pipeline..."
echo "Pipeline: $PIPELINE"
echo ""
eval "$PIPELINE"

echo ""
echo "=============================================================="
echo " Done"
echo "=============================================================="
echo "  Event clips:       $OUTPUT_DIR/event-clips/"
echo "  Selected frames:   $OUTPUT_DIR/selected_frames/"
echo "  Scaled ROI frames: $OUTPUT_DIR/roi_scaled_frames/"
echo "=============================================================="
