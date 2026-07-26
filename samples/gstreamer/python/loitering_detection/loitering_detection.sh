#!/bin/bash
# ==============================================================================
# Copyright (C) 2021-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# /opt/intel/dlstreamer/samples/download_public_models.sh yolo11s coco128

set -e

# Configuration parameters
MODELS_PATH=${MODELS_PATH:-./models}  # Path to models directory (e.g., /path/to/omz_models)
INPUT=${1:-https://github.com/open-edge-platform/edge-ai-resources/raw/refs/heads/main/videos/VIRAT_S_000101.mp4}
MODEL=${2:-${MODELS_PATH}/public/yolo11s/FP16/yolo11s.xml} # Detection model (YOLO, SSD, etc.)
OUTPUT=${3:-loitering_detection_output.mp4}  # Output video file (H.264 MP4)
DEVICE=${4:-"GPU"}

# Configuration file that defines the zone "pathway" for loitering detection 
CONFIG_FILE=./virat_s_000101-config.json 

export GST_PLUGIN_PATH=./plugins:${GST_PLUGIN_PATH}
export GI_TYPELIB_PATH=/opt/intel/dlstreamer/gstreamer/lib/girepository-1.0:/opt/intel/dlstreamer/lib/girepository-1.0:/usr/lib/x86_64-linux-gnu/girepository-1.0
export PYTHONPATH=./plugins:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:/opt/intel/dlstreamer/python:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:${PYTHONPATH}

rm -rf ~/.cache/gstreamer-1.0/registry.x86_64.bin

if [ ! -f "$MODEL" ]; then
    echo "Model file not found: $MODEL"
    exit 1
fi

if [[ ${DEVICE} == "CPU" ]]; then
    GVADETECT_OPTIONS="device=CPU pre-process-backend=opencv batch-size=8 nireq=2"
    ENCODER="videoconvert ! openh264enc"
    WATERMARK_DEVICE="CPU"
elif [[ ${DEVICE} == "GPU" ]]; then
    GVADETECT_OPTIONS="device=GPU pre-process-backend=va-surface-sharing batch-size=8 nireq=2"
    ENCODER="vah264enc"
    WATERMARK_DEVICE="GPU"
elif [[ ${DEVICE} == "NPU" ]]; then
    GVADETECT_OPTIONS="device=NPU pre-process-backend=va batch-size=1 nireq=2"
    ENCODER="vah264enc"
    WATERMARK_DEVICE="GPU"
else
    echo "Error: Unsupported device: $DEVICE"
    exit 1
fi

set -x
gst-launch-1.0 -e urisourcebin uri=${INPUT} ! decodebin3 ! \
    gvadetect model=$MODEL ${GVADETECT_OPTIONS} ! queue ! \
    gvatrack tracking-type=zero-term ! queue ! \
    gvaanalytics config=${CONFIG_FILE}  ! queue ! \
    loitering_watermark loitering-threshold=4.0 ! queue ! \
    gvawatermark device=${WATERMARK_DEVICE} ! \
    gvafpscounter ! queue ! \
    ${ENCODER} bitrate=1024 ! h264parse ! mp4mux ! filesink location=${OUTPUT}
