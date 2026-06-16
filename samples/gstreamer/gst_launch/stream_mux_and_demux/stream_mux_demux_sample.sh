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
DEFAULT_SYNC_MODE="none"
ENABLE_DEMUX=false
MODEL_PATH=""

# Lidar (heterogeneous / container mode) defaults
DEFAULT_LIDAR_LOCATION="velodyne/%06d.bin"
DEFAULT_LIDAR_START_INDEX="0"
DEFAULT_LIDAR_FRAME_RATE="10"
ENABLE_LIDAR=false
LIDAR_LOCATION="${DEFAULT_LIDAR_LOCATION}"
LIDAR_START_INDEX="${DEFAULT_LIDAR_START_INDEX}"
LIDAR_FRAME_RATE="${DEFAULT_LIDAR_FRAME_RATE}"

# Function to display usage information
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Multi-Stream Inference with gvastreammux and gvastreamdemux"
    echo ""
    echo "Options:"
    echo "  -m, --model PATH        Path to inference model XML file"
    echo "                          (required for video-only/PASSTHROUGH mode; ignored with --lidar)"
    echo "  -s, --source TYPE       Source type: file or rtsp (default: ${DEFAULT_SOURCE})"
    echo "  -i, --input1 URI        First input source URI"
    echo "                          Default (rtsp): ${DEFAULT_INPUT1}"
    echo "  -j, --input2 URI        Second input source URI"
    echo "                          Default (rtsp): ${DEFAULT_INPUT2}"
    echo "      --demux             Enable gvastreamdemux for per-source output"
    echo "      --max-fps FPS       Set max-fps (for local file sources only, default: ${DEFAULT_MAX_FPS})"
    echo "      --sync-mode MODE    PTS normalization across pads: none|first-pts|segment|pipeline|ntp"
    echo "                          (default: ${DEFAULT_SYNC_MODE})"
    echo "  -d, --device DEVICE     Inference device: GPU, CPU, NPU (default: ${DEFAULT_DEVICE})"
    echo ""
    echo "  Heterogeneous (video + lidar) mode — switches the mux to CONTAINER output:"
    echo "      --lidar             Add a lidar source (application/x-lidar via g3dlidarparse)."
    echo "                          Forces gvastreamdemux (a container batch must be unpacked"
    echo "                          before any per-stream element). No gvadetect is inserted;"
    echo "                          each demuxed branch goes straight to fakesink."
    echo "      --lidar-location L  multifilesrc location pattern (default: ${DEFAULT_LIDAR_LOCATION})"
    echo "      --lidar-start-index N  multifilesrc start-index (default: ${DEFAULT_LIDAR_START_INDEX})"
    echo "      --lidar-frame-rate F   g3dlidarparse frame-rate, frames/sec (default: ${DEFAULT_LIDAR_FRAME_RATE})"
    echo ""
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
    echo "  # Multiple files with different PTS bases (e.g. clips recorded at different times)"
    echo "  $0 -m model.xml -s file -i clip1.mp4 -j clip2.mp4 --sync-mode first-pts"
    echo ""
    echo "  # NTP-synchronized IP cameras (rtspsrc must have add-reference-timestamp-meta=true upstream)"
    echo "  $0 -m model.xml -s rtsp --sync-mode ntp"
    echo ""
    echo "  # Heterogeneous: 2 video files + 1 lidar sequence, demuxed to fakesinks"
    echo "  $0 -s file -i v0.h265 -j v1.h265 --lidar --lidar-location 'velodyne/%06d.bin' --sync-mode first-pts"
    echo ""
}

# Initialize variables
SOURCE_TYPE="${DEFAULT_SOURCE}"
INPUT1="${DEFAULT_INPUT1}"
INPUT2="${DEFAULT_INPUT2}"
DEVICE="${DEFAULT_DEVICE}"
MAX_FPS="${DEFAULT_MAX_FPS}"
SYNC_MODE="${DEFAULT_SYNC_MODE}"

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
        --sync-mode)
            SYNC_MODE="$2"
            shift 2
            ;;
        --lidar)
            ENABLE_LIDAR=true
            shift
            ;;
        --lidar-location)
            LIDAR_LOCATION="$2"
            shift 2
            ;;
        --lidar-start-index)
            LIDAR_START_INDEX="$2"
            shift 2
            ;;
        --lidar-frame-rate)
            LIDAR_FRAME_RATE="$2"
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

# A lidar source makes the inputs heterogeneous: the mux switches to CONTAINER
# output (multistream/x-analytics-batch). A container buffer is NOT raw video,
# so no video element (gvadetect) may follow the mux directly — it must be
# unpacked by gvastreamdemux first. Force demux on and skip the model check.
if [ "${ENABLE_LIDAR}" = true ]; then
    ENABLE_DEMUX=true
fi

# Validate model path (only needed for video-only PASSTHROUGH pipelines)
if [ "${ENABLE_LIDAR}" = false ]; then
    if [ -z "${MODEL_PATH}" ]; then
        echo "ERROR: Model path is required. Use -m or --model to specify." >&2
        show_usage
        exit 1
    fi

    if [ ! -f "${MODEL_PATH}" ]; then
        echo "ERROR: Model file not found: ${MODEL_PATH}" >&2
        exit 1
    fi
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

if [ "${ENABLE_LIDAR}" = true ]; then
    if ! gst-inspect-1.0 g3dlidarparse > /dev/null 2>&1; then
        echo "ERROR: g3dlidarparse element not found (required for --lidar)!" >&2
        echo "Please ensure DL Streamer is properly compiled and installed." >&2
        exit 1
    fi
fi

# Validate sync-mode
case "${SYNC_MODE}" in
    none|first-pts|segment|pipeline|ntp) ;;
    *)
        echo "ERROR: Invalid --sync-mode '${SYNC_MODE}'. Allowed: none|first-pts|segment|pipeline|ntp." >&2
        exit 1
        ;;
esac

# Build mux property string
MUX_PROPS="sync-mode=${SYNC_MODE}"
if [ "${SOURCE_TYPE}" = "file" ] && [ "${MAX_FPS}" != "0" ]; then
    MUX_PROPS="${MUX_PROPS} max-fps=${MAX_FPS}"
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

if [ "${ENABLE_LIDAR}" = true ]; then
    echo "  Source 2: ${LIDAR_LOCATION} (lidar, start-index=${LIDAR_START_INDEX}, frame-rate=${LIDAR_FRAME_RATE})"
else
    echo "  Model:     ${MODEL_PATH}"
    echo "  Device:    ${DEVICE}"
fi
echo "  Demux:     ${ENABLE_DEMUX}"
echo "  Sync mode: ${SYNC_MODE}"
echo "  Mode:      $([ "${ENABLE_LIDAR}" = true ] && echo 'CONTAINER (video + lidar)' || echo 'PASSTHROUGH (video only)')"
echo ""

# Build pipeline
if [ "${ENABLE_LIDAR}" = true ]; then
    # ----------------------------------------------------------------------
    # Heterogeneous (CONTAINER) mode: video + lidar.
    # The mux emits multistream/x-analytics-batch container buffers; demux
    # unpacks them back to per-source pads. Each branch goes straight to
    # fakesink — gvadetect (video) and g3dinference (lidar) are intentionally
    # NOT inserted here, as wiring per-stream inference downstream of the demux
    # is out of scope for this sample.
    #   src_0 -> video, src_1 -> video, src_2 -> lidar (application/x-lidar)
    # ----------------------------------------------------------------------
    LIDAR_SOURCE="multifilesrc location=${LIDAR_LOCATION} start-index=${LIDAR_START_INDEX} caps=application/octet-stream ! g3dlidarparse frame-rate=${LIDAR_FRAME_RATE}"

    PIPELINE="gst-launch-1.0 -e \
        gvastreammux name=mux ${MUX_PROPS} \
        ! queue \
        ! gvastreamdemux name=demux \
        demux.src_0 ! queue ! gvafpscounter ! fakesink \
        demux.src_1 ! queue ! gvafpscounter ! fakesink \
        demux.src_2 ! queue ! fakesink \
        ${SOURCE_0} ! mux.sink_0 \
        ${SOURCE_1} ! mux.sink_1 \
        ${LIDAR_SOURCE} ! mux.sink_2"
elif [ "${ENABLE_DEMUX}" = true ]; then
    # Build inference element
    INFERENCE="gvadetect model=${MODEL_PATH} device=${DEVICE}"
    if [ "${DEVICE}" = "GPU" ]; then
        INFERENCE="${INFERENCE} pre-process-backend=va-surface-sharing"
    fi
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
    # Build inference element
    INFERENCE="gvadetect model=${MODEL_PATH} device=${DEVICE}"
    if [ "${DEVICE}" = "GPU" ]; then
        INFERENCE="${INFERENCE} pre-process-backend=va-surface-sharing"
    fi
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
