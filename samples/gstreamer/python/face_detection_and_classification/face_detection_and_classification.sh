#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

# List help message
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: $0 [INPUT] [DEVICE] [OUTPUT]"
  echo ""
  echo "Arguments:"
  echo "  INPUT   - Local video file (default: download sample video)"
  echo "  DEVICE  - Inference device: CPU or GPU (default: GPU)"
  echo "  OUTPUT  - Output mode: file or json (default: file)"
  echo ""
  exit 0
fi

INPUT=${1:-}
DEVICE=${2:-GPU}
OUTPUT=${3:-file}

python3 "$(dirname "$0")"/face_detection_and_classification.py "${INPUT}" "${DEVICE}" "${OUTPUT}"
