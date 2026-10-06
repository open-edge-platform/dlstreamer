#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# This sample fetches front-camera frames + calibration from PandaSet
# (https://pandaset.org), (c) Hesai & Scale AI, distributed under CC BY 4.0,
# and assembles them into a short mp4. The data is downloaded on demand and is
# NOT redistributed with this repository.
# ==============================================================================

set -euo pipefail

# List help message
if [[ "${1:-}" == "--help" ]] || [[ "${1:-}" == "-h" ]]; then
  echo "Usage: $0 [MODEL] [DEVICE] [OUTPUT] [THRESHOLD] [SEQUENCE] [NUM_FRAMES]"
  echo ""
  echo "Arguments:"
  echo "  MODEL      - Path to the MonoDETR OpenVINO IR (.xml)."
  echo "               Default: \${MODELS_PATH}/public/monodetr/FP16/monodetr.xml"
  echo "               Produce it with scripts/download_models/convert_monodetr.py"
  echo "  DEVICE     - Inference device (default: GPU). Supported: CPU, GPU"
  echo "  OUTPUT     - Output type (default: file). Supported: file, display, json, display-and-json"
  echo "  THRESHOLD  - Detection confidence threshold (default: 0.3)"
  echo "  SEQUENCE   - PandaSet sequence id (default: 001)"
  echo "  NUM_FRAMES - Number of frames to fetch/assemble (default: 79)"
  echo ""
  exit 0
fi

MODEL=${1:-"${MODELS_PATH:-}/public/monodetr/FP16/monodetr.xml"}
DEVICE=${2:-"GPU"}         # Supported values: CPU, GPU
OUTPUT=${3:-"file"}        # Supported values: file, display, json, display-and-json
THRESHOLD=${4:-"0.3"}
SEQUENCE=${5:-"001"}
NUM_FRAMES=${6:-"79"}
FRAMERATE=10               # PandaSet cameras run at ~10 Hz

# Resolve the script location so helper files still resolve, but keep the working directory as the
# caller's so data and outputs are written where the script was invoked from.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="data"
# Per-sequence cache dir so switching SEQUENCE re-fetches instead of reusing another sequence's frames.
SEQ_DIR="${DATA_DIR}/${SEQUENCE}"
CALIB="${SEQ_DIR}/calibration.json"
VIDEO="${DATA_DIR}/pandaset_${SEQUENCE}.mp4"

if [ ! -f "$MODEL" ]; then
  echo "Error: MonoDETR model not found: ${MODEL}" >&2
  echo "Provide the IR path as the first argument, or generate it with:" >&2
  echo "  python3 ${SCRIPT_DIR}/../../../../scripts/download_models/convert_monodetr.py --outdir ." >&2
  exit 1
fi

if [[ `uname` != "MINGW64"* ]]; then
  if [ -z "$(find /dev/dri/ -name 'render*' 2>/dev/null)" ] && [[ "$DEVICE" == "GPU" ]]; then
    echo "WARN: No GPU detected, switching to CPU"
    DEVICE=CPU
  fi
fi

# Select an available H.264 (or fallback) encode chain from system-memory raw video to mp4mux.
pick_encode_chain() {
  if gst-inspect-1.0 vah264enc >/dev/null 2>&1; then
    echo "vapostproc ! vah264enc ! h264parse ! mp4mux"
  elif gst-inspect-1.0 vah264lpenc >/dev/null 2>&1; then
    echo "vapostproc ! vah264lpenc ! h264parse ! mp4mux"
  elif gst-inspect-1.0 x264enc >/dev/null 2>&1; then
    echo "x264enc ! h264parse ! mp4mux"
  elif gst-inspect-1.0 avenc_mpeg4 >/dev/null 2>&1; then
    echo "avenc_mpeg4 ! mp4mux"
  else
    echo "Error - no H.264/MPEG-4 encoder found (vah264enc/vah264lpenc/x264enc/avenc_mpeg4)." >&2
    exit 1
  fi
}
ENCODE_CHAIN="$(pick_encode_chain)"

# Fetch PandaSet frames + calibration on demand (cached per sequence). Re-fetch when the requested
# frame count changes so NUM_FRAMES takes effect even if a different-sized clip was cached before.
REQUESTED_FRAMES="$NUM_FRAMES"
cached_requested=""
[ -f "${SEQ_DIR}/crop.env" ] && cached_requested="$(sed -n 's/^REQUESTED_FRAMES=//p' "${SEQ_DIR}/crop.env")"
if [ ! -f "$CALIB" ] || [ ! -f "${SEQ_DIR}/crop.env" ] || [ "$cached_requested" != "$REQUESTED_FRAMES" ]; then
  rm -rf "$SEQ_DIR" "$VIDEO"
  python3 "${SCRIPT_DIR}/download_pandaset_sample.py" --out "$SEQ_DIR" --seq "$SEQUENCE" --num "$REQUESTED_FRAMES"
fi
# shellcheck source=/dev/null
source "${SEQ_DIR}/crop.env"

# Assemble the frames into a short mp4 once (cached). Frames are already reprojected to KITTI
# intrinsics by the downloader, so no cropping is needed here.
if [ ! -f "$VIDEO" ]; then
  echo "[info] assembling ${NUM_FRAMES} frames into ${VIDEO}"
  gst-launch-1.0 -q multifilesrc location="${SEQ_DIR}/frames/%04d.jpg" start-index=0 \
    stop-index=$((NUM_FRAMES - 1)) loop=false caps=image/jpeg,framerate=${FRAMERATE}/1 ! \
    jpegdec ! videoconvert ! ${ENCODE_CHAIN} ! filesink location="$VIDEO"
fi

SOURCE_ELEMENT="filesrc location=${VIDEO} ! decodebin3 ! videoconvert"

if [[ "$OUTPUT" == "display" ]]; then
  SINK_ELEMENT="gvawatermark3d calibration-file=${CALIB} ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ "$OUTPUT" == "json" ]]; then
  rm -f output.json
  SINK_ELEMENT="gvametaconvert ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false"
elif [[ "$OUTPUT" == "display-and-json" ]]; then
  rm -f output.json
  SINK_ELEMENT="gvawatermark3d calibration-file=${CALIB} ! gvametaconvert ! \
gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ "$OUTPUT" == "file" ]]; then
  rm -f "monocular_3d_detection_${DEVICE}.mp4"
  SINK_ELEMENT="gvawatermark3d calibration-file=${CALIB} ! videoconvert ! gvafpscounter ! \
${ENCODE_CHAIN} ! filesink location=monocular_3d_detection_${DEVICE}.mp4"
else
  echo "Error: wrong value for OUTPUT parameter" >&2
  echo "Valid values: file, display, json, display-and-json" >&2
  exit 1
fi

PIPELINE="gst-launch-1.0 ${SOURCE_ELEMENT} ! \
gvamono3d model=${MODEL} device=${DEVICE} calibration-file=${CALIB} threshold=${THRESHOLD} ! queue ! \
${SINK_ELEMENT}"

echo "${PIPELINE}"
eval "${PIPELINE}"
