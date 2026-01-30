#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DLSTREAMER_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"

# Default values
DEFAULT_DATA_PATH="${DLSTREAMER_ROOT}/raddet"
DEFAULT_CONFIG_PATH="${DLSTREAMER_ROOT}/raddet/RadarConfig_raddet.json"
DEFAULT_FRAME_RATE="0"
DEFAULT_START_INDEX="559"
DEFAULT_OUTPUT_PATH="radar_results.json"

# Function to display usage information
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Radar Signal Processing with g3dradarprocess element"
    echo ""
    echo "Options:"
    echo "  -d, --data-path PATH          Path to radar data directory (containing radar/ subfolder)"
    echo "                                Default: ${DEFAULT_DATA_PATH}"
    echo "  -c, --config PATH             Path to radar configuration JSON file"
    echo "                                Default: ${DEFAULT_CONFIG_PATH}"
    echo "  -f, --frame-rate RATE         Target frame rate for output (0 = no limit)"
    echo "                                Default: ${DEFAULT_FRAME_RATE}"
    echo "  -s, --start-index INDEX       Starting frame index"
    echo "                                Default: ${DEFAULT_START_INDEX}"
    echo "  -o, --output PATH             Path for JSON output file (enables publishing)"
    echo "                                Default: ${DEFAULT_OUTPUT_PATH}"
    echo "  -h, --help                    Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0"
    echo "  $0 --data-path /path/to/raddet --config /path/to/config.json"
    echo "  $0 --frame-rate 30 --output results.json"
    echo "  $0 --start-index 600 --output my_results.json"
    echo "  GST_DEBUG=g3dradarprocess:4 $0 --frame-rate 10"
    echo ""
}

# Initialize variables with defaults
DATA_PATH="${DEFAULT_DATA_PATH}"
CONFIG_PATH="${DEFAULT_CONFIG_PATH}"
FRAME_RATE="${DEFAULT_FRAME_RATE}"
START_INDEX="${DEFAULT_START_INDEX}"
OUTPUT_PATH=""

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--data-path)
            DATA_PATH="$2"
            shift 2
            ;;
        -c|--config)
            CONFIG_PATH="$2"
            shift 2
            ;;
        -f|--frame-rate)
            FRAME_RATE="$2"
            shift 2
            ;;
        -s|--start-index)
            START_INDEX="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_PATH="$2"
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

# Check if g3dradarprocess element is available
if ! gst-inspect-1.0 g3dradarprocess > /dev/null 2>&1; then
    echo "ERROR: g3dradarprocess element not found!" >&2
    echo "Please ensure DL Streamer is properly compiled with 3D elements support." >&2
    echo "You can verify by running: gst-inspect-1.0 g3dradarprocess" >&2
    exit 1
fi

# Check if data path exists
if [ ! -d "${DATA_PATH}/radar" ]; then
    echo "ERROR: Radar data directory not found: ${DATA_PATH}/radar" >&2
    echo "" >&2
    echo "Please download the radar data first:" >&2
    echo "  cd \"${DLSTREAMER_ROOT}\"" >&2
    echo "  wget --no-proxy https://af01p-igk.devtools.intel.com/artifactory/platform_hero-igk-local/RadarData/Radar_raddet_data.zip" >&2
    echo "  unzip Radar_raddet_data.zip" >&2
    echo "  rm Radar_raddet_data.zip" >&2
    exit 1
fi

# Check if config file exists
if [ ! -f "${CONFIG_PATH}" ]; then
    echo "ERROR: Radar configuration file not found: ${CONFIG_PATH}" >&2
    echo "Please verify the configuration file path is correct." >&2
    exit 1
fi

# Check if oneAPI environment is sourced (check for icpx or other oneAPI tools)
if ! command -v icpx &> /dev/null && [ ! -d "/opt/intel/oneapi" ]; then
    echo "WARNING: Intel oneAPI might not be installed or sourced." >&2
    echo "The g3dradarprocess element requires oneAPI and libradar." >&2
    echo "" >&2
    echo "To install dependencies, run:" >&2
    echo "  \"${DLSTREAMER_ROOT}/scripts/install_radar_dependencies.sh\"" >&2
    echo "" >&2
    echo "If already installed, source the environment:" >&2
    echo "  source /opt/intel/oneapi/setvars.sh" >&2
    echo "" >&2
fi

# Construct file location pattern
RADAR_FILES_PATTERN="${DATA_PATH}/radar/%06d.bin"

# Print configuration
echo "========================================"
echo "Radar Signal Processing Sample"
echo "========================================"
echo "Data path:      ${DATA_PATH}/radar"
echo "Config file:    ${CONFIG_PATH}"
echo "File pattern:   ${RADAR_FILES_PATTERN}"
echo "Start index:    ${START_INDEX}"
echo "Frame rate:     ${FRAME_RATE} fps (0 = unlimited)"
if [ -n "${OUTPUT_PATH}" ]; then
    echo "Output file:    ${OUTPUT_PATH}"
else
    echo "Output file:    (none - processing only)"
fi
echo "========================================"
echo ""

# Build GStreamer pipeline
PIPELINE="multifilesrc location=\"${RADAR_FILES_PATTERN}\" start-index=${START_INDEX} ! "
PIPELINE+="application/octet-stream ! "
PIPELINE+="g3dradarprocess radar-config=\"${CONFIG_PATH}\" frame-rate=${FRAME_RATE} ! "

# Add metadata publishing if output path is specified
if [ -n "${OUTPUT_PATH}" ]; then
    PIPELINE+="gvametaconvert format=json json-indent=2 ! "
    PIPELINE+="gvametapublish file-format=2 file-path=\"${OUTPUT_PATH}\" ! "
fi

PIPELINE+="fakesink"

# Print pipeline (useful for debugging)
if [ -n "${GST_DEBUG:-}" ]; then
    echo "Pipeline: ${PIPELINE}"
    echo ""
fi

# Run the pipeline
echo "Starting radar signal processing..."
echo "Press Ctrl+C to stop"
echo ""

eval gst-launch-1.0 "${PIPELINE}"

# Check if output file was created
if [ -n "${OUTPUT_PATH}" ] && [ -f "${OUTPUT_PATH}" ]; then
    echo ""
    echo "========================================"
    echo "Results saved to: ${OUTPUT_PATH}"
    
    # Show sample of results if jq is available
    if command -v jq &> /dev/null; then
        echo "========================================"
        echo "Sample output (first frame):"
        jq '.' "${OUTPUT_PATH}" 2>/dev/null | head -50 || head -20 < "${OUTPUT_PATH}"
    fi
fi

echo ""
echo "Processing completed successfully!"
