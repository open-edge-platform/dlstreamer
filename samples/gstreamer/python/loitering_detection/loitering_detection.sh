#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# Display usage information
usage() {
    cat << EOF
Usage: $(basename "$0") [INPUT] [CONFIG_FILE] [MODEL] [DEVICE] [OUTPUT]

Description:
  Detects loitering behavior in video using object detection and tracking.
  Processes video input, applies YOLO detection, tracks objects, and marks
  loitering zones based on the configuration file.

Parameters:
  INPUT       - Input video (file path or URL)
                Default: https://github.com/open-edge-platform/edge-ai-resources/raw/refs/heads/main/videos/VIRAT_S_000101.mp4
  
  CONFIG_FILE - Zone configuration file for loitering detection
                Default: ./virat_s_000101-config.json
  
  MODEL       - Detection model (path to .xml file for OpenVINO format)
                Default: \${MODELS_PATH}/public/yolo11s/FP16/yolo11s.xml
                The default path is built from the MODELS_PATH environment variable
                (default: ./models). Set MODELS_PATH to point to your models directory.
  
  DEVICE      - Processing device (CPU, GPU, or NPU)
                Default: GPU
                Supported: CPU | GPU | NPU
  
  OUTPUT      - Output video file (H.264 MP4 format)
                Default: loitering_detection_output.mp4

Examples:
  # Run with defaults
  $(basename "$0")
  
  # Use CPU device with custom config and model
  $(basename "$0") input.mp4 config.json ./models/yolo11s.xml CPU output.mp4
  
  # Process with GPU (explicit)
  $(basename "$0") video.mp4 config.json ./models/detection.xml GPU result.mp4
  
  # Use NPU device
  $(basename "$0") input.mp4 config.json ./models/yolo11s.xml NPU output.mp4
  
  # Override models directory via environment variable
  MODELS_PATH=/opt/my_models $(basename "$0") input.mp4 config.json "" GPU output.mp4

EOF
    exit 0
}

set -e

# Handle help flag
if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    usage
fi

# Configuration parameters
MODELS_PATH=${MODELS_PATH:-./models}  # Path to models directory (e.g., /path/to/omz_models)
INPUT=${1:-https://github.com/open-edge-platform/edge-ai-resources/raw/refs/heads/main/videos/VIRAT_S_000101.mp4}
CONFIG_FILE=${2:-./virat_s_000101-config.json}
MODEL=${3:-${MODELS_PATH}/public/yolo11s/FP16/yolo11s.xml} # Detection model (YOLO, SSD, etc.)
DEVICE=${4:-"GPU"}
OUTPUT=${5:-loitering_detection_output.mp4}  # Output video file (H.264 MP4)

export GST_PLUGIN_PATH=./plugins:${GST_PLUGIN_PATH}
export GI_TYPELIB_PATH=/opt/intel/dlstreamer/gstreamer/lib/girepository-1.0:/opt/intel/dlstreamer/lib/girepository-1.0:/usr/lib/x86_64-linux-gnu/girepository-1.0
export PYTHONPATH=./plugins:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:/opt/intel/dlstreamer/python:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:${PYTHONPATH}

rm -rf ~/.cache/gstreamer-1.0/registry.x86_64.bin

# Validate parameters
if [ ! -f "$MODEL" ]; then
    echo "Error: Model file not found: $MODEL"
    echo "Run '$(basename "$0") --help' for usage information."
    exit 1
fi

# Validate DEVICE parameter
if [[ ! "$DEVICE" =~ ^(CPU|GPU|NPU)$ ]]; then
    echo "Error: Unsupported device: $DEVICE"
    echo "Supported devices: CPU, GPU, NPU"
    echo "Run '$(basename "$0") --help' for usage information."
    exit 1
fi

# Validate INPUT and select source element
if [[ "$INPUT" =~ ^https?:// ]]; then
    SOURCE_ELEMENT="urisourcebin uri=${INPUT}"
else
    if [ ! -f "$INPUT" ]; then
        echo "Error: Input file not found: $INPUT"
        echo "Run '$(basename "$0") --help' for usage information."
        exit 1
    fi
    SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

# Validate CONFIG_FILE exists (optional but recommended)
if [ ! -f "$CONFIG_FILE" ]; then
    echo "Warning: Config file not found: $CONFIG_FILE"
    echo "Loitering detection may not work as expected."
fi

if [[ ${DEVICE} == "CPU" ]]; then
    GVADETECT_OPTIONS="device=CPU pre-process-backend=opencv batch-size=8 nireq=2"
    WATERMARK_DEVICE="CPU"
elif [[ ${DEVICE} == "GPU" ]]; then
    GVADETECT_OPTIONS="device=GPU pre-process-backend=va-surface-sharing batch-size=8 nireq=2"
    WATERMARK_DEVICE="GPU"
elif [[ ${DEVICE} == "NPU" ]]; then
    GVADETECT_OPTIONS="device=NPU pre-process-backend=va batch-size=1 nireq=2"
    WATERMARK_DEVICE="GPU"
else
    echo "Error: Unsupported device: $DEVICE"
    exit 1
fi

# Use vah264enc if available, otherwise fallback to openh264enc
if gst-inspect-1.0 vah264enc &>/dev/null; then
    ENCODER="vah264enc"
else
    ENCODER="videoconvert ! openh264enc"
fi

set -x
gst-launch-1.0 -e ${SOURCE_ELEMENT} ! decodebin3 ! \
    gvadetect model=${MODEL} ${GVADETECT_OPTIONS} ! queue ! \
    gvatrack tracking-type=zero-term ! queue ! \
    gvaanalytics config=${CONFIG_FILE}  ! queue ! \
    loitering_watermark loitering-threshold=4.0 ! queue ! \
    gvawatermark device=${WATERMARK_DEVICE} ! \
    gvafpscounter ! queue ! \
    ${ENCODER} bitrate=1024 ! h264parse ! mp4mux ! filesink location=${OUTPUT}
