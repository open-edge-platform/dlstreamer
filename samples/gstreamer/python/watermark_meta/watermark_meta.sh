#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

INPUT=${1:-https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4}
OUTPUT=${2:-/tmp/watermark_meta_output.mp4}

echo "Running watermark_meta sample"
echo "  Input:  ${INPUT}"
echo "  Output: ${OUTPUT}"

python3 "$(dirname "$0")"/watermark_meta.py "${INPUT}" "${OUTPUT}"

echo "Done. Output written to: ${OUTPUT}"
