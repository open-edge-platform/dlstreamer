#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

# Usage: ./gvatagger_motion.sh [INPUT|synthetic] [OUTPUT_MP4] [extra gvatagger_motion.py options]
INPUT=${1:-synthetic}
OUTPUT=${2:-/tmp/gvatagger_motion_output.mp4}
ENCODER=${ENCODER:-openh264}
K=${K:-1.5}
shift $(( $# < 2 ? $# : 2 ))

ARGS=(--output "${OUTPUT}" --k "${K}")
[ -n "${GST_DEBUG:-}" ] && ARGS+=(--gst-debug "${GST_DEBUG}")
if [ "${INPUT}" = "synthetic" ]; then
    ARGS+=(--encoder "${ENCODER}")
else
    ARGS+=(--input "${INPUT}")
fi

echo "Running gvatagger_motion sample"
echo "  Input:  ${INPUT}$([ "${INPUT}" = "synthetic" ] && echo " (encoder: ${ENCODER})")"
echo "  Output: ${OUTPUT}"
echo "  k:      ${K}"
[ -n "${GST_DEBUG:-}" ] && echo "  GST_DEBUG: ${GST_DEBUG}"

python3 "$(dirname "$0")"/gvatagger_motion.py "${ARGS[@]}" "$@"
