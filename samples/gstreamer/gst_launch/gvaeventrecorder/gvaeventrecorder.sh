#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ===============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_VIDEO_URL="https://github.com/intel-iot-devkit/sample-videos/raw/master/people-detection.mp4"
DEFAULT_VIDEO="$SCRIPT_DIR/video/people-detection.mp4"
DEFAULT_MODEL="$SCRIPT_DIR/models/public/yolov8s/FP16/yolov8s.xml"
DEFAULT_OUTPUT_DIR="$SCRIPT_DIR/output"

rel_path() {
    realpath -m --relative-to=. -- "$1"
}

show_usage() {
    echo "Usage: $0 [--input <video.mp4>] [--model <model.xml>] [--output <directory>]"
    echo "Options (an omitted option uses its default):"
    echo "  --input <video.mp4>    Input video (default: $(rel_path "$DEFAULT_VIDEO"), downloaded if missing)"
    echo "  --model <model.xml>    OpenVINO detection model (default: $(rel_path "$DEFAULT_MODEL"), exported if missing)"
    echo "  --output <directory>   Output directory (default: $(rel_path "$DEFAULT_OUTPUT_DIR"))"
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

INPUT_VIDEO=$DEFAULT_VIDEO
DETECTION_MODEL=$DEFAULT_MODEL
OUTPUT_DIR=$DEFAULT_OUTPUT_DIR

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

if [[ "$INPUT_VIDEO" == "$DEFAULT_VIDEO" && ! -s "$DEFAULT_VIDEO" ]]; then
    download_default_video
fi

if [[ "$DETECTION_MODEL" == "$DEFAULT_MODEL" && ! -s "$DEFAULT_MODEL" ]]; then
    download_default_model
fi

if [[ -z "$OUTPUT_DIR" ]]; then
    echo "Error: output directory must not be empty" >&2
    show_help_hint
    exit 1
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
