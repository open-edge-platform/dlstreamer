#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# People Detection & Tracking with Deep SORT
#
# Pipeline:
#   filesrc → decodebin3 →
#   gvadetect (YOLO26m) → queue →
#   gvainference (mars-small128, per-ROI) → queue →
#   gvatrack (deep-sort) → queue →
#   gvafpscounter → gvawatermark →
#   gvametaconvert → gvametapublish (JSONL) →
#   videoconvert → vah264enc → h264parse → mp4mux → filesink
#
# Optimized for Intel Core Ultra 3 processors.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODELS_DIR="${SCRIPT_DIR}/models"
RESULTS_DIR="${SCRIPT_DIR}/results"

# ── Help ─────────────────────────────────────────────────────────────────────

if [[ "${1:-}" == "--help" ]] || [[ "${1:-}" == "-h" ]]; then
  echo "Usage: $0 [INPUT] [DEVICE]"
  echo ""
  echo "Arguments:"
  echo "  INPUT   - Input video file path (default: videos/people.mp4)"
  echo "  DEVICE  - Inference device (default: GPU). Supported: CPU, GPU"
  echo ""
  echo "Output:"
  echo "  results/people_detection_tracking-<video>.mp4   - Annotated output video"
  echo "  results/people_detection_tracking-<video>.jsonl  - Detection metadata"
  exit 0
fi

# ── Parameters ───────────────────────────────────────────────────────────────

INPUT="${1:-${SCRIPT_DIR}/videos/people.mp4}"
DEVICE="${2:-GPU}"

# ── Validate input ───────────────────────────────────────────────────────────

if [[ ! -f "${INPUT}" ]]; then
  echo "Error: Input video not found: ${INPUT}" >&2
  echo "Download it first:" >&2
  echo "  mkdir -p videos && curl -L -o videos/people.mp4 \\" >&2
  echo "    -H 'Referer: https://www.pexels.com/' \\" >&2
  echo "    -H 'User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36' \\" >&2
  echo "    'https://videos.pexels.com/video-files/18552655/18552655-hd_1280_720_30fps.mp4'" >&2
  exit 1
fi

# ── Locate models ────────────────────────────────────────────────────────────

DETECT_MODEL=$(find "${MODELS_DIR}/yolo26m_openvino" -name "*.xml" 2>/dev/null | head -1)
if [[ -z "${DETECT_MODEL}" ]]; then
  echo "Error: YOLO26m model not found. Run: python3 export_models.py" >&2
  exit 1
fi

REID_MODEL="${MODELS_DIR}/mars-small128/mars_small128_fp32.xml"
if [[ ! -f "${REID_MODEL}" ]]; then
  echo "Error: Mars-Small128 model not found. Run: python3 export_models.py" >&2
  exit 1
fi

echo "Detection model: ${DETECT_MODEL}"
echo "Re-ID model:     ${REID_MODEL}"
echo "Input:           ${INPUT}"
echo "Device:          ${DEVICE}"

# ── Device fallback ──────────────────────────────────────────────────────────

if [[ "${DEVICE}" == "GPU" ]] && [[ ! -d /dev/dri ]]; then
  echo "Warning: GPU not available, falling back to CPU"
  DEVICE="CPU"
fi

# ── Select encoder ───────────────────────────────────────────────────────────

if gst-inspect-1.0 va 2>/dev/null | grep -q vah264enc; then
  ENCODER="vah264enc"
elif gst-inspect-1.0 va 2>/dev/null | grep -q vah264lpenc; then
  ENCODER="vah264lpenc"
else
  echo "Warning: VA-API H.264 encoder not found, using x264enc"
  ENCODER="x264enc tune=zerolatency"
fi

# ── Output paths ─────────────────────────────────────────────────────────────

mkdir -p "${RESULTS_DIR}"
FILE_STEM="$(basename "${INPUT%.*}")"
OUTPUT_VIDEO="${RESULTS_DIR}/people_detection_tracking-${FILE_STEM}.mp4"
OUTPUT_JSON="${RESULTS_DIR}/people_detection_tracking-${FILE_STEM}.jsonl"

rm -f "${OUTPUT_VIDEO}" "${OUTPUT_JSON}"

# ── Build and run pipeline ───────────────────────────────────────────────────

PIPELINE="gst-launch-1.0 \
  filesrc location=\"${INPUT}\" ! \
  decodebin3 caps=\"video/x-raw(ANY)\" ! \
  gvadetect model=\"${DETECT_MODEL}\" device=${DEVICE} batch-size=4 ! \
  queue ! \
  gvainference model=\"${REID_MODEL}\" device=${DEVICE} inference-region=roi-list object-class=person ! \
  queue ! \
  gvatrack tracking-type=deep-sort \
    deepsort-trck-cfg=\"max_age=60,max_cosine_distance=0.3,object_class=person,reid_max_age=30\" ! \
  queue ! \
  gvafpscounter ! \
  gvawatermark displ-cfg=show-roi=person ! \
  gvametaconvert ! \
  gvametapublish file-format=json-lines file-path=\"${OUTPUT_JSON}\" ! \
  videoconvert ! ${ENCODER} ! h264parse ! \
  mp4mux fragment-duration=1000 ! \
  filesink location=\"${OUTPUT_VIDEO}\""

echo ""
echo "Pipeline:"
echo "${PIPELINE}"
echo ""
echo "Running pipeline (first run may take several minutes for model compilation)..."

eval "${PIPELINE}"

echo ""
echo "Pipeline complete."
echo "Output video: ${OUTPUT_VIDEO}"
echo "Output JSON:  ${OUTPUT_JSON}"
