#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

# Default values
DEFAULT_SOURCE="rtsp"
DEFAULT_INPUT1="rtsp://localhost:8554/stream"
DEFAULT_INPUT2="rtsp://localhost:8555/stream"
DEFAULT_DEVICE="GPU"
DEFAULT_MAX_FPS="0"
ENABLE_DEMUX=false
MODEL_PATH=""

# Function to display usage information
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Multi-Stream Inference with gvastreammux and gvastreamdemux"
    echo ""
    echo "Options:"
    echo "  -m, --model PATH        Path to inference model XML file (required)"
    echo "  -s, --source TYPE       Source type: file or rtsp (default: ${DEFAULT_SOURCE})"
    echo "  -i, --input1 URI        First input source URI"
    echo "                          Default (rtsp): ${DEFAULT_INPUT1}"
    echo "  -j, --input2 URI        Second input source URI"
    echo "                          Default (rtsp): ${DEFAULT_INPUT2}"
    echo "      --demux             Enable gvastreamdemux for per-source output"
    echo "      --max-fps FPS       Set max-fps (for local file sources only, default: ${DEFAULT_MAX_FPS})"
    echo "  -d, --device DEVICE     Inference device: GPU, CPU, NPU (default: ${DEFAULT_DEVICE})"
    echo "  -h, --help              Show this help message"
    echo ""
    echo "Examples:"
    echo "  # Two RTSP streams, shared inference"
    echo "  $0 -m model.xml -s rtsp"
    echo ""
    echo "  # Two RTSP streams with per-source demux"
    echo "  $0 -m model.xml -s rtsp --demux"
    echo ""
    echo "  # Two local files with max-fps"
    echo "  $0 -m model.xml -s file -i video0.mp4 -j video1.mp4 --max-fps 30"
    echo ""
    echo "  # Local files with demux and debug"
    echo "  GST_DEBUG=gvastreammux:4,gvastreamdemux:4 $0 -m model.xml -s file -i v0.mp4 -j v1.mp4 --max-fps 30 --demux"
    echo ""
}

# Initialize variables
SOURCE_TYPE="${DEFAULT_SOURCE}"
INPUT1="${DEFAULT_INPUT1}"
INPUT2="${DEFAULT_INPUT2}"
DEVICE="${DEFAULT_DEVICE}"
MAX_FPS="${DEFAULT_MAX_FPS}"

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -m|--model)
            MODEL_PATH="$2"
            shift 2
            ;;
        -s|--source)
            SOURCE_TYPE="$2"
            shift 2
            ;;
        -i|--input1)
            INPUT1="$2"
            shift 2
            ;;
        -j|--input2)
            INPUT2="$2"
            shift 2
            ;;
        --demux)
            ENABLE_DEMUX=true
            shift
            ;;
        --max-fps)
            MAX_FPS="$2"
            shift 2
            ;;
        -d|--device)
            DEVICE="$2"
            shift 2
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        *)
            echo "ERROR: Unknown option: $1" >&2
            show_usage
            exit 1
            ;;
    esac
done

# Validate model path
if [ -z "${MODEL_PATH}" ]; then
    echo "ERROR: Model path is required. Use -m or --model to specify." >&2
    show_usage
    exit 1
fi

if [ ! -f "${MODEL_PATH}" ]; then
    echo "ERROR: Model file not found: ${MODEL_PATH}" >&2
    exit 1
fi

# Check element availability
if ! gst-inspect-1.0 gvastreammux > /dev/null 2>&1; then
    echo "ERROR: gvastreammux element not found!" >&2
    echo "Please ensure DL Streamer is properly compiled and installed." >&2
    echo "  make build && sudo -E make install" >&2
    exit 1
fi

if [ "${ENABLE_DEMUX}" = true ]; then
    if ! gst-inspect-1.0 gvastreamdemux > /dev/null 2>&1; then
        echo "ERROR: gvastreamdemux element not found!" >&2
        echo "Please ensure DL Streamer is properly compiled and installed." >&2
        exit 1
    fi
fi

# Build max-fps property string
MUX_PROPS=""
if [ "${SOURCE_TYPE}" = "file" ] && [ "${MAX_FPS}" != "0" ]; then
    MUX_PROPS="max-fps=${MAX_FPS}"
fi

# Build source branches based on source type
if [ "${SOURCE_TYPE}" = "rtsp" ]; then
    SOURCE_0="rtspsrc location=${INPUT1} latency=200 ! rtph265depay ! h265parse ! vah265dec"
    SOURCE_1="rtspsrc location=${INPUT2} latency=200 ! rtph265depay ! h265parse ! vah265dec"
    echo "=== Multi-Stream RTSP Inference ==="
    echo "  Source 0: ${INPUT1}"
    echo "  Source 1: ${INPUT2}"
elif [ "${SOURCE_TYPE}" = "file" ]; then
    # Validate files exist
    if [ ! -f "${INPUT1}" ]; then
        echo "ERROR: Input file not found: ${INPUT1}" >&2
        exit 1
    fi
    if [ ! -f "${INPUT2}" ]; then
        echo "ERROR: Input file not found: ${INPUT2}" >&2
        exit 1
    fi
    SOURCE_0="filesrc location=${INPUT1} ! h265parse ! vah265dec"
    SOURCE_1="filesrc location=${INPUT2} ! h265parse ! vah265dec"
    echo "=== Multi-Stream File Inference ==="
    echo "  Source 0: ${INPUT1}"
    echo "  Source 1: ${INPUT2}"
    if [ "${MAX_FPS}" != "0" ]; then
        echo "  Max FPS:  ${MAX_FPS}"
    fi
else
    echo "ERROR: Unknown source type: ${SOURCE_TYPE}. Use 'file' or 'rtsp'." >&2
    exit 1
fi

echo "  Model:    ${MODEL_PATH}"
echo "  Device:   ${DEVICE}"
echo "  Demux:    ${ENABLE_DEMUX}"
echo ""

# Build inference element
INFERENCE="gvadetect model=${MODEL_PATH} device=${DEVICE}"
if [ "${DEVICE}" = "GPU" ]; then
    INFERENCE="${INFERENCE} pre-process-backend=va-surface-sharing"
fi

# Build pipeline
if [ "${ENABLE_DEMUX}" = true ]; then
    # With demux: per-source FPS counters
    PIPELINE="gst-launch-1.0 \
        gvastreammux name=mux ${MUX_PROPS} \
        ! queue \
        ! ${INFERENCE} \
        ! gvastreamdemux name=demux \
        demux.src_0 ! queue ! gvafpscounter ! fakesink \
        demux.src_1 ! queue ! gvafpscounter ! fakesink \
        ${SOURCE_0} ! mux.sink_0 \
        ${SOURCE_1} ! mux.sink_1"
else
    # Without demux: single combined FPS counter
    PIPELINE="gst-launch-1.0 \
        gvastreammux name=mux ${MUX_PROPS} \
        ! queue \
        ! ${INFERENCE} \
        ! queue ! gvafpscounter ! fakesink \
        ${SOURCE_0} ! mux.sink_0 \
        ${SOURCE_1} ! mux.sink_1"
fi

echo "Running pipeline..."
echo ""
echo "${PIPELINE}"
echo ""

# Execute pipeline
eval "${PIPELINE}"
