#!/bin/bash
# ==============================================================================
# Copyright (C) 2020-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

#If you want to use your own input file, provide an absolute path
FILE=${1:-https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4}
OUTPUT=${2:-json} # Valid values: display, display-and-json, json, file
DEVICE=${3:-CPU} # Valid devices: CPU, GPU,NPU?
OUTPUT_DIRECTORY=${4:-/home/dlstreamer/} #Path where to copy output.json

. /etc/os-release

if [ -z "${MODELS_PATH:-}" ]; then
  echo "Error: MODELS_PATH is not set." >&2
  exit 1
else
  echo "MODELS_PATH: $MODELS_PATH"
fi

BASE_DIR="$(realpath "$(dirname "$0")")"
BUILD_DIR=$HOME/build

if [ -d "${BUILD_DIR}" ]; then
  rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}" || exit

export PKG_CONFIG_PATH=/opt/intel/dlstreamer/gstreamer/lib/pkgconfig:/opt/intel/dlstreamer/lib/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig

CMAKE_OPENCV_FLAGS=(-DCMAKE_C_FLAGS=-I/opt/opencv/include -DCMAKE_CXX_FLAGS=-I/opt/opencv/include -DCMAKE_EXE_LINKER_FLAGS=-L/opt/opencv)

if [ -f /etc/lsb-release ]; then
    cmake "${BASE_DIR}" "${CMAKE_OPENCV_FLAGS[@]}"
else
    cmake3 "${BASE_DIR}" "${CMAKE_OPENCV_FLAGS[@]}"
fi

make -j "$(nproc)"

"${BUILD_DIR}"/draw_face_attributes -i "$FILE" -o "$OUTPUT" -d "$DEVICE"
cp "${BUILD_DIR}/output.json" "$OUTPUT_DIRECTORY"