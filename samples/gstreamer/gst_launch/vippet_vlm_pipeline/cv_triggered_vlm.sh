#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#
# CV-Triggered VLM — Object detection triggers VLM inference via proximity analysis
#
# Pipeline: filesrc → decode → detect(NPU) → tee
#   Branch 1: watermark → encode → video file
#   Branch 2: proximity trigger → VLM(GPU) → JSON output
#

set -e

SCRIPTDIR="$(dirname "$(realpath "$0")")"

# ── Defaults ─────────────────────────────────────────────────────────────────
INPUT="${1:-${SCRIPTDIR}/videos/pedestrians.mp4}"
DETECT_DEVICE="${2:-NPU}"
VLM_DEVICE="${3:-GPU}"

DETECT_MODEL="${DETECT_MODEL:-${SCRIPTDIR}/models/yolo11s_openvino/yolo11s.xml}"
VLM_MODEL="${VLM_MODEL:-${SCRIPTDIR}/models/InternVL3_5-2B}"
VLM_PROMPT="${VLM_PROMPT:-Are people riding on bicycles? Answer yes or no.}"

CLASS_A="${CLASS_A:-person}"
CLASS_B="${CLASS_B:-bicycle}"
PROXIMITY_DISTANCE="${PROXIMITY_DISTANCE:-150}"
PROXIMITY_FRAMES="${PROXIMITY_FRAMES:-10}"

RESULTS_DIR="${SCRIPTDIR}/results"
mkdir -p "${RESULTS_DIR}"

OUTPUT_VIDEO="${RESULTS_DIR}/output.mp4"
OUTPUT_JSON="${RESULTS_DIR}/vlm_output.jsonl"

# ── Validate inputs ──────────────────────────────────────────────────────────
if [[ ! -f "${INPUT}" ]]; then
  echo "Error: Input video not found: ${INPUT}"
  echo "Download it first — see README.md"
  exit 1
fi

if [[ ! -f "${DETECT_MODEL}" ]]; then
  echo "Error: Detection model not found: ${DETECT_MODEL}"
  echo "Run 'python3 export_models.py' first — see README.md"
  exit 1
fi

if [[ ! -d "${VLM_MODEL}" ]]; then
  echo "Error: VLM model not found: ${VLM_MODEL}"
  echo "Run 'python3 export_models.py' first — see README.md"
  exit 1
fi

# ── Source element ────────────────────────────────────────────────────────────
if [[ "${INPUT}" == *"://"* ]]; then
  SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${INPUT}"
else
  SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

echo "=== CV-Triggered VLM Pipeline ==="
echo "Input:          ${INPUT}"
echo "Detect device:  ${DETECT_DEVICE}"
echo "Detect model:   ${DETECT_MODEL}"
echo "VLM device:     ${VLM_DEVICE}"
echo "VLM model:      ${VLM_MODEL}"
echo "VLM prompt:     ${VLM_PROMPT}"
echo "Proximity:      ${CLASS_A} <-> ${CLASS_B}, distance=${PROXIMITY_DISTANCE}px, frames=${PROXIMITY_FRAMES}"
echo "Output video:   ${OUTPUT_VIDEO}"
echo "Output JSON:    ${OUTPUT_JSON}"
echo ""

PIPELINE="gst-launch-1.0 \
${SOURCE_ELEMENT} ! decodebin3 caps=video/x-raw(ANY) ! \
gvadetect model=${DETECT_MODEL} device=${DETECT_DEVICE} ! queue ! \
tee name=t \
t. ! queue flush-on-eos=true ! gvawatermark ! gvafpscounter ! \
    vah264enc ! h264parse ! mp4mux fragment-duration=1000 ! \
    filesink location=${OUTPUT_VIDEO} \
t. ! queue leaky=downstream max-size-buffers=1 flush-on-eos=true ! \
    gvaproximitytrigger_py class-a=${CLASS_A} class-b=${CLASS_B} distance=${PROXIMITY_DISTANCE} frames=${PROXIMITY_FRAMES} ! \
    videoconvertscale ! video/x-raw,width=796,height=448 ! \
    gvagenai model-path=${VLM_MODEL} device=${VLM_DEVICE} \
        prompt=\"${VLM_PROMPT}\" \
        generation-config=\"max_new_tokens=10\" \
        chunk-size=1 metrics=true ! \
    gvametapublish file-format=json-lines file-path=${OUTPUT_JSON} ! \
    jpegenc ! multifilesink location=${RESULTS_DIR}/vlm_frame_%05d.jpeg async=false"

echo "Pipeline: ${PIPELINE}"
echo ""

GST_PLUGIN_PATH="${GST_PLUGIN_PATH:+${GST_PLUGIN_PATH}:}${SCRIPTDIR}/plugins" \
GST_REGISTRY_FORK=no \
${PIPELINE}
