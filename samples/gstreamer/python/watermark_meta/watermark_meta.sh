#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

INPUT=${1:-}
if [[ -z "$INPUT" ]]; then
	echo "Error: provide an input video path, stream URL, or camera device as the first argument." >&2
	exit 2
fi
OUTPUT=${2:-/tmp/watermark_meta_output.mp4}

echo "Running watermark_meta sample"
echo "  Input:  ${INPUT}"
echo "  Output: ${OUTPUT}"

python3 "$(dirname "$0")"/watermark_meta.py "${INPUT}" "${OUTPUT}"

echo "Done. Output written to: ${OUTPUT}"
