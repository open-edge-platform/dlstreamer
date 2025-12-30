#!/bin/bash
# ==============================================================================
# Copyright (C) 2021-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# This sample refers to a video file by Rihard-Clement-Ciprian Diac via Pexels
# (https://www.pexels.com)
# ==============================================================================

set -euo pipefail

# Default values
DEFAULT_SOURCE="https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4"
DEFAULT_DEVICE="CPU"
DEFAULT_OUTPUT="json"
DEFAULT_MODEL="clip-vit-large-patch14"
DEFAULT_PPBKEND="opencv"

# Check if MODELS_PATH is set
if [ -z "$MODELS_PATH" ]; then
    echo "ERROR - MODELS_PATH is not set." >&2
    exit 1
fi

SUPPORTED_MODELS=(
  "clip-vit-large-patch14"
  "clip-vit-base-patch16"
  "clip-vit-base-patch32"
)

# Print help message
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    echo "Usage: $0 [SOURCE_FILE] [DEVICE] [OUTPUT] [MODEL] [PREPROCESSING_BACKEND]"
    echo ""
    echo "Arguments:"
    echo "  SOURCE_FILE            - Input source (default: Pexels video URL)"
    echo "  DEVICE                 - Device (default: CPU). Supported: CPU, GPU"
    echo "  OUTPUT                 - Output type (default: json). Supported: json, fps"
    echo "  MODEL                  - Model name (default: clip-vit-large-patch14). Supported: ${SUPPORTED_MODELS[*]}"
    echo "  PREPROCESSING_BACKEND  - Preprocessing backend (default: opencv). Supported: ie, opencv, va, va-surface-sharing"
    echo ""
    exit 0
fi

# Arguments
SOURCE_FILE=${1:-$DEFAULT_SOURCE}
DEVICE=${2:-$DEFAULT_DEVICE}
OUTPUT=${3:-$DEFAULT_OUTPUT}
MODEL=${4:-$DEFAULT_MODEL}
PPBKEND=${5:-$DEFAULT_PPBKEND}

if ! [[ " ${SUPPORTED_MODELS[*]} " =~ [[:space:]]${MODEL}[[:space:]] ]]; then
  echo "Unsupported model: $MODEL" >&2
  exit 1
fi

# Print MODELS_PATH
echo "MODELS_PATH: $MODELS_PATH"

# Construct the model path
MODEL_PATH="${MODELS_PATH}/public/${MODEL}/FP32/${MODEL}.xml"

# Check if model exists in local directory
if [ ! -f "$MODEL_PATH" ]; then
    echo "ERROR - model not found: $MODEL_PATH" >&2
    exit 1
fi

# Determine the source element based on the input
if [[ "$SOURCE_FILE" == "/dev/video"* ]]; then
    SOURCE_ELEMENT="v4l2src device=${SOURCE_FILE}"
elif [[ "$SOURCE_FILE" == *"://"* ]]; then
    SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${SOURCE_FILE}"
else
    SOURCE_ELEMENT="filesrc location=${SOURCE_FILE}"
fi

# Create the DL Streamer pipeline based on the device and output
if [ "$DEVICE" == "CPU" ]; then
    if [ "$OUTPUT" == "json" ]; then
        PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3 ! videoconvert ! videoscale ! gvainference model=\"$MODEL_PATH\" device=CPU pre-process-backend=opencv ! gvametaconvert format=json add-tensor-data=true ! gvametapublish method=file file-path=output.json ! fakesink"
    elif [ "$OUTPUT" == "fps" ]; then
        PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3 ! videoconvert ! videoscale ! gvainference model=\"$MODEL_PATH\" device=CPU pre-process-backend=opencv ! gvafpscounter ! fakesink"
    else
        echo "Invalid output specified. Use file or fps."
        exit 1
    fi
elif [ "$DEVICE" == "GPU" ]; then
    if [ "$OUTPUT" == "json" ]; then
        PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3 ! videoconvert ! videoscale ! vapostproc ! \"video/x-raw(memory:VAMemory)\" ! gvainference model=\"$MODEL_PATH\" ie-config=INFERENCE_PRECISION_HINT=f32 device=GPU pre-process-backend=$PPBKEND ! gvametaconvert format=json add-tensor-data=true ! gvametapublish method=file file-path=output.json ! fakesink"
    elif [ "$OUTPUT" == "fps" ]; then
        PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT ! decodebin3 ! videoconvert ! videoscale ! vapostproc ! \"video/x-raw(memory:VAMemory)\" ! gvainference model=\"$MODEL_PATH\" ie-config=INFERENCE_PRECISION_HINT=f32 device=GPU pre-process-backend=$PPBKEND ! gvafpscounter ! fakesink"
    else
        echo "Invalid output specified. Use json or fps."
        exit 1
    fi
else
    echo "Invalid device specified. Use CPU or GPU."
    exit 1
fi

# Print and run the pipeline
echo "Running pipeline on $DEVICE with source file $SOURCE_FILE, model path $MODEL_PATH, and output $OUTPUT"
echo "Pipeline: $PIPELINE"
eval "$PIPELINE"