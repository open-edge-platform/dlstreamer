#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

# Usage: ./gvatrigger_motion.sh [INPUT|synthetic] [OUTPUT_MP4] [extra gvatrigger_motion.py options]
INPUT=${1:-synthetic}
OUTPUT=${2:-}
ENCODER=${ENCODER:-openh264}
K=${K:-2.0}
ALPHA=${ALPHA:-0.03}
MARGIN=${MARGIN:-64}
MAX_CACHE_TIME_MS=${MAX_CACHE_TIME_MS:-4000}
MIN_BASELINE=${MIN_BASELINE:-100}
shift $(( $# < 2 ? $# : 2 ))

ARGS=(--k "${K}" --alpha "${ALPHA}" --margin "${MARGIN}" --max-cache-time-ms "${MAX_CACHE_TIME_MS}" --min-baseline "${MIN_BASELINE}")
[ -n "${OUTPUT}" ] && ARGS+=(--output "${OUTPUT}")
[ -n "${GST_DEBUG:-}" ] && ARGS+=(--gst-debug "${GST_DEBUG}")
if [ "${INPUT}" = "synthetic" ]; then
    ARGS+=(--encoder "${ENCODER}")
else
    ARGS+=(--input "${INPUT}")
fi

echo "Running gvatrigger_motion sample"
echo "  Input:  ${INPUT}$([ "${INPUT}" = "synthetic" ] && echo " (encoder: ${ENCODER})")"
[ -n "${OUTPUT}" ] && echo "  Output: ${OUTPUT}"
echo "  k:      ${K}"
echo "  alpha:  ${ALPHA}"
echo "  margin: ${MARGIN}"
echo "  max-cache-time-ms: ${MAX_CACHE_TIME_MS}"
echo "  min-baseline:      ${MIN_BASELINE}"
[ -n "${GST_DEBUG:-}" ] && echo "  GST_DEBUG: ${GST_DEBUG}"

python3 "$(dirname "$0")"/gvatrigger_motion.py "${ARGS[@]}" "$@"
