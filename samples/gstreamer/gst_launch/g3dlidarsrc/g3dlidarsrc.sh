#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

# Resolve paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DLSTREAMER_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"

# Default values
DEFAULT_CONFIG_PATH="${DLSTREAMER_ROOT}/src/monolithic/gst/3d_elements/g3dlidarsrc/configs/robosense_e1r_udp.json"
DEFAULT_NTP_SYNC="false"
DEFAULT_TIMEOUT="5000000"   # 5 s in microseconds
DEFAULT_STREAM_ID="0"
DEFAULT_OUTPUT_PATH=""      # empty = no JSON publishing
DEFAULT_MODEL_CONFIG=""     # empty = capture only (no g3dinference)

show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Live LiDAR capture with the g3dlidarsrc element."
    echo ""
    echo "Options:"
    echo "  -c, --config PATH        Path to the LiDAR JSON config (vendor/model/transport)."
    echo "                           Default: ${DEFAULT_CONFIG_PATH}"
    echo "  -n, --ntp-sync BOOL      true = use the LiDAR hardware clock, false = pipeline clock."
    echo "                           Default: ${DEFAULT_NTP_SYNC}"
    echo "  -t, --timeout USEC       No-data timeout in microseconds (0 = disabled)."
    echo "                           Default: ${DEFAULT_TIMEOUT}"
    echo "  -i, --stream-id ID       Stream identifier written into LidarMeta."
    echo "                           Default: ${DEFAULT_STREAM_ID}"
    echo "  -m, --model-config PATH  Optional g3dinference config JSON. When set, the pipeline"
    echo "                           runs 3D detection (g3dlidarsrc ! g3dinference ! ...)."
    echo "  -o, --output PATH        Optional JSON output file."
    echo "                           Without --model-config: publish source metadata."
    echo "                           With --model-config: publish inference metadata."
    echo "  -h, --help               Show this help message."
    echo ""
    echo "Examples:"
    echo "  # Basic capture (verify the device is streaming)"
    echo "  $0"
    echo ""
    echo "  # Capture from a specific NIC bind + LiDAR clock timestamps"
    echo "  $0 --config /path/to/lidar.json --ntp-sync true"
    echo ""
    echo "  # Full 3D detection pipeline with JSON export"
    echo "  $0 --model-config pointpillars_ov_config.json --output detections.json"
    echo ""
    echo "  # Verbose element logging"
    echo "  GST_DEBUG=g3dlidarsrc:5 $0"
    echo ""
}

# Parse arguments
CONFIG_PATH="${DEFAULT_CONFIG_PATH}"
NTP_SYNC="${DEFAULT_NTP_SYNC}"
TIMEOUT="${DEFAULT_TIMEOUT}"
STREAM_ID="${DEFAULT_STREAM_ID}"
OUTPUT_PATH="${DEFAULT_OUTPUT_PATH}"
MODEL_CONFIG="${DEFAULT_MODEL_CONFIG}"

while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--config)       CONFIG_PATH="$2"; shift 2 ;;
        -n|--ntp-sync)     NTP_SYNC="$2"; shift 2 ;;
        -t|--timeout)      TIMEOUT="$2"; shift 2 ;;
        -i|--stream-id)    STREAM_ID="$2"; shift 2 ;;
        -m|--model-config) MODEL_CONFIG="$2"; shift 2 ;;
        -o|--output)       OUTPUT_PATH="$2"; shift 2 ;;
        -h|--help)         show_usage; exit 0 ;;
        *) echo "ERROR: Unknown option: $1" >&2; show_usage; exit 1 ;;
    esac
done

# --- Preconditions ----------------------------------------------------------

# 1) The element must be registered (plugin built and on GST_PLUGIN_PATH).
if ! gst-inspect-1.0 g3dlidarsrc > /dev/null 2>&1; then
    echo "ERROR: g3dlidarsrc element not found." >&2
    echo "Build the 3D elements plugin and put it on GST_PLUGIN_PATH." >&2
    echo "Verify with: gst-inspect-1.0 g3dlidarsrc" >&2
    exit 1
fi

# 2) The vendor backend must be loadable at runtime. g3dlidarsrc derives the
#    library name from the config `vendor` field and dlopen()s
#    libg3dlidar_<vendor>.so, which is only built when its backend is enabled
#    (e.g. -DENABLE_LIDAR_ROBOSENSE=ON, OFF by default) and must be on
#    LD_LIBRARY_PATH. This is a soft check against the config's vendor: warn
#    (don't fail) so the element's own start() error stays authoritative.
VENDOR="$(grep -oE '"vendor"[[:space:]]*:[[:space:]]*"[^"]+"' "${CONFIG_PATH}" 2>/dev/null | head -1 | sed -E 's/.*"([^"]+)"$/\1/')"
if [ -n "${VENDOR}" ]; then
    BACKEND_LIB="libg3dlidar_${VENDOR}.so"
    if ! ( ldconfig -p 2>/dev/null | grep -q "${BACKEND_LIB}" ) \
       && ! ( echo "${LD_LIBRARY_PATH:-}" | tr ':' '\n' | while read -r d; do
                  [ -n "$d" ] && [ -f "$d/${BACKEND_LIB}" ] && exit 0; done ); then
        VENDOR_UC="$(echo "${VENDOR}" | tr '[:lower:]' '[:upper:]')"
        echo "WARNING: ${BACKEND_LIB} was not found on the library path." >&2
        echo "  g3dlidarsrc loads it at start() via dlopen. Build it with:" >&2
        echo "    cmake -B build -S \"${DLSTREAMER_ROOT}\" -DENABLE_LIDAR_${VENDOR_UC}=ON" >&2
        echo "    cmake --build build --target g3dlidar_${VENDOR}" >&2
        echo "  then add its directory (e.g. build/intel64/Release/lib) to LD_LIBRARY_PATH." >&2
        echo "" >&2
    fi
fi

# 3) The config file must exist.
if [ ! -f "${CONFIG_PATH}" ]; then
    echo "ERROR: LiDAR config file not found: ${CONFIG_PATH}" >&2
    exit 1
fi

# --- Report -----------------------------------------------------------------

echo "========================================"
echo "g3dlidarsrc Live Capture Sample"
echo "========================================"
echo "Config file:  ${CONFIG_PATH}"
echo "ntp-sync:     ${NTP_SYNC}"
echo "timeout:      ${TIMEOUT} us"
echo "stream-id:    ${STREAM_ID}"
echo "Model config: ${MODEL_CONFIG:-(none - capture only)}"
echo "Output file:  ${OUTPUT_PATH:-(none)}"
echo "========================================"
echo ""

# --- Build pipeline ---------------------------------------------------------

PIPELINE="g3dlidarsrc config=\"${CONFIG_PATH}\" ntp-sync=${NTP_SYNC} timeout=${TIMEOUT} stream-id=${STREAM_ID} ! "

if [ -n "${MODEL_CONFIG}" ]; then
    PIPELINE+="g3dinference config=\"${MODEL_CONFIG}\" device=CPU ! "
fi

if [ -n "${OUTPUT_PATH}" ]; then
    # Publish source metadata without model, or include tensors when inference is enabled.
    if [ -n "${MODEL_CONFIG}" ]; then
        PIPELINE+="gvametaconvert add-tensor-data=true format=json json-indent=2 ! "
    else
        PIPELINE+="gvametaconvert format=json json-indent=2 ! "
    fi
    PIPELINE+="gvametapublish file-format=2 file-path=\"${OUTPUT_PATH}\" ! "
fi

PIPELINE+="fakesink"

if [ -n "${GST_DEBUG:-}" ]; then
    echo "Pipeline: ${PIPELINE}"
    echo ""
fi

echo "Starting capture... (Ctrl+C to stop)"
echo "If no device is streaming, the element exits after the timeout with a"
echo "RESOURCE/READ error - that is expected without a connected LiDAR."
echo ""

eval gst-launch-1.0 "${PIPELINE}"

if [ -n "${OUTPUT_PATH}" ] && [ -f "${OUTPUT_PATH}" ]; then
    echo ""
    echo "========================================"
    echo "Results saved to: ${OUTPUT_PATH}"
    if command -v jq &> /dev/null; then
        echo "Sample output (first frame):"
        jq '.' "${OUTPUT_PATH}" 2>/dev/null | head -50 || head -20 < "${OUTPUT_PATH}"
    fi
fi

echo ""
echo "Done."
