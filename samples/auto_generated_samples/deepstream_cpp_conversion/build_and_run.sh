#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#
# Build and run the DeepStream LPR conversion sample.
#
# Usage:
#   ./build_and_run.sh [--input <video_path>] [--device GPU|CPU] [--display]
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "=== Building deepstream-lpr-app ==="
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake ..
make -j"$(nproc)"

echo ""
echo "=== Build complete ==="
echo "Binary: ${BUILD_DIR}/deepstream-lpr-app"
echo ""

# Run if --input was provided
if [[ $# -gt 0 ]]; then
    echo "=== Running ==="
    "${BUILD_DIR}/deepstream-lpr-app" "$@"
fi
