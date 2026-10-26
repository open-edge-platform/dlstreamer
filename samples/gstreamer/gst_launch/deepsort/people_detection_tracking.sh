#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# This sample refers to a video file by Khaoula ATTAR via Pexels
# (https://www.pexels.com)
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
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODELS_DIR="${HOME}/models"
RESULTS_DIR="${SCRIPT_DIR}/results"

# ── Help ─────────────────────────────────────────────────────────────────────

if [[ "${1:-}" == "--help" ]] || [[ "${1:-}" == "-h" ]]; then
  echo "Usage: $0 [INPUT] [DEVICE] [OUTPUT] [INTERVAL] [FRAMES_DIR]"
  echo ""
  echo "Arguments:"
  echo "  INPUT      - Input video file path (default: videos/people.mp4)"
  echo "  DEVICE     - Inference device (default: GPU). Supported: CPU, GPU"
  echo "  OUTPUT     - Output artifact to produce (default: mp4). One of: json, mp4, frames"
  echo "  INTERVAL   - Only used when OUTPUT=frames. Save roughly every Nth processed"
  echo "               frame as a PNG, dropping the rest (default: 1 = save all)."
  echo "               Detection/tracking still run on every frame (identical results"
  echo "               to mp4/json); INTERVAL only thins out the saved PNGs, via a"
  echo "               videorate stage placed right before pngenc."
  echo "  FRAMES_DIR - Only used when OUTPUT=frames. Overrides the directory PNG"
  echo "               frames are written to (default: results/people_detection_tracking-<video>_frames/)."
  echo ""
  echo "Output:"
  echo "  OUTPUT=mp4    -> results/people_detection_tracking-<video>.mp4    (annotated video)"
  echo "  OUTPUT=json   -> results/people_detection_tracking-<video>.jsonl  (detection metadata)"
  echo "  OUTPUT=frames -> results/people_detection_tracking-<video>_frames/ (every annotated PNG frame)"
  exit 0
fi

# ── Parameters ───────────────────────────────────────────────────────────────

DEFAULT_INPUT="${SCRIPT_DIR}/videos/people.mp4"
INPUT="${1:-${DEFAULT_INPUT}}"
DEVICE="${2:-GPU}"
OUTPUT="${3:-mp4}"
INTERVAL="${4:-1}"
FRAMES_DIR_OVERRIDE="${5:-}"

if [[ "${OUTPUT}" != "json" ]] && [[ "${OUTPUT}" != "mp4" ]] && [[ "${OUTPUT}" != "frames" ]]; then
  echo "Error: OUTPUT must be one of: json, mp4, frames (got: ${OUTPUT})" >&2
  exit 1
fi

if ! [[ "${INTERVAL}" =~ ^[1-9][0-9]*$ ]]; then
  echo "Error: INTERVAL must be a positive integer (got: ${INTERVAL})" >&2
  exit 1
fi

# ── Validate input ───────────────────────────────────────────────────────────

if [[ ! -f "${INPUT}" ]]; then
  if [[ "${INPUT}" == "${DEFAULT_INPUT}" ]]; then
    echo "No input given and default sample not found, downloading it..."
    mkdir -p "$(dirname "${DEFAULT_INPUT}")"
    curl -L -o "${DEFAULT_INPUT}" \
      -H 'Referer: https://www.pexels.com/' \
      -H 'User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36' \
      'https://www.pexels.com/download/video/39182823/?fps=24.0&h=720&w=1280'
  else
    echo "Error: Input video not found: ${INPUT}" >&2
    exit 1
  fi
fi

# ── Locate models ────────────────────────────────────────────────────────────

DETECT_MODEL=$(find "${MODELS_DIR}/public/yolo26m/FP16" -name "*.xml" 2>/dev/null | head -1)
if [[ -z "${DETECT_MODEL}" ]]; then
  echo "Error: YOLO26m model not found." >&2
  exit 1
fi

REID_MODEL="${MODELS_DIR}/public/mars-small128/FP32/mars_small128_fp32.xml"
if [[ ! -f "${REID_MODEL}" ]]; then
  echo "Error: Mars-Small128 model not found." >&2
  exit 1
fi

echo "Detection model: ${DETECT_MODEL}"
echo "Re-ID model:     ${REID_MODEL}"
echo "Input:           ${INPUT}"
echo "Device:          ${DEVICE}"
if [[ "${OUTPUT}" == "frames" ]]; then
  echo "Interval:        ${INTERVAL} (1 = save every frame)"
fi

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

FILE_STEM="$(basename "${INPUT%.*}")"
mkdir -p "${RESULTS_DIR}"
OUTPUT_VIDEO="${RESULTS_DIR}/people_detection_tracking-${DEVICE}_${FILE_STEM}.mp4"
OUTPUT_JSON="${RESULTS_DIR}/people_detection_tracking-${DEVICE}_${FILE_STEM}.jsonl"
FRAMES_DIR="${RESULTS_DIR}/people_detection_tracking-${DEVICE}_${FILE_STEM}_frames"
if [[ -n "${FRAMES_DIR_OVERRIDE}" ]]; then
  FRAMES_DIR="${FRAMES_DIR_OVERRIDE}"
fi

case "${OUTPUT}" in
  mp4) rm -f "${OUTPUT_VIDEO}" ;;
  json) rm -f "${OUTPUT_JSON}" ;;
  frames) rm -rf "${FRAMES_DIR}"; mkdir -p "${FRAMES_DIR}" ;;
esac

# ── Build and run pipeline ───────────────────────────────────────────────────

case "${OUTPUT}" in
  mp4)
    TAIL="videoconvert ! ${ENCODER} ! h264parse ! \
  mp4mux fragment-duration=1000 ! \
  filesink location=\"${OUTPUT_VIDEO}\""
    ;;
  json)
    TAIL="gvametaconvert ! \
  gvametapublish file-format=json-lines file-path=\"${OUTPUT_JSON}\" ! \
  fakesink sync=false"
    ;;
  frames)
    # INTERVAL (> 1) thins out the PNGs written below via videorate, placed
    # after gvawatermark so it never affects gvadetect/gvainference/gvatrack:
    # every frame is still detected and tracked identically to the mp4/json
    # outputs, only the saved PNG count changes.
    PNG_INTERVAL_STAGE=""
    if [[ "${INTERVAL}" != "1" ]]; then
      SRC_FPS=24 # hardcoded for the default videos/people.mp4 (downloaded at fps=24.0)
      TARGET_FPS="${SRC_FPS}/${INTERVAL}"
      echo "PNG downsampling: ${SRC_FPS} fps -> ${TARGET_FPS} fps (videorate)"
      PNG_INTERVAL_STAGE="videorate ! \"video/x-raw,framerate=${TARGET_FPS}\" ! "
    fi
    TAIL="vapostproc ! videoconvert ! ${PNG_INTERVAL_STAGE}pngenc ! \
  multifilesink location=\"${FRAMES_DIR}/frame_%05d.png\""
    ;;
esac

PIPELINE="gst-launch-1.0 \
  filesrc location=\"${INPUT}\" ! \
  decodebin3 caps=\"video/x-raw(ANY)\" ! \
  gvadetect model=\"${DETECT_MODEL}\" device=${DEVICE} batch-size=1 threshold=0.6 ! \
  queue ! \
  gvainference model=\"${REID_MODEL}\" device=${DEVICE} inference-region=roi-list object-class=person ! \
  queue ! \
  gvatrack tracking-type=deep-sort \
    deepsort-trck-cfg=\"max_age=30,max_cosine_distance=0.3,max_iou_distance=0.6,object_class=person,reid_max_age=30\" ! \
  queue ! \
  gvafpscounter ! \
  gvawatermark displ-cfg=show-roi=person,font-scale=0.8 ! \
  ${TAIL}"

echo ""
echo "Pipeline:"
echo "${PIPELINE}"
echo ""
echo "Running pipeline (first run may take several minutes for model compilation)..."

eval "${PIPELINE}"

echo ""
echo "Pipeline complete."
case "${OUTPUT}" in
  mp4) echo "Output video: ${OUTPUT_VIDEO}" ;;
  json) echo "Output JSON:  ${OUTPUT_JSON}" ;;
  frames) echo "PNG frames:   ${FRAMES_DIR}" ;;
esac
