#!/bin/bash
# ==============================================================================
# Copyright (C) 2020-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

DEVICE=${1:-"GPU"}  # Supported values: CPU, GPU, NPU
OUTPUT=${2:-"file"} # Supported values: file, display, fps, json, display-and-json
INPUT=${3:-"https://github.com/intel-iot-devkit/sample-videos/raw/master/head-pose-face-detection-female-and-male.mp4"}

if [ -z "${MODELS_PATH:-}" ]; then
	echo "Error: MODELS_PATH is not set." >&2
	exit 1
else
	echo "MODELS_PATH: $MODELS_PATH"
fi

# Check help message
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
	echo "Usage: $0 [DEVICE] [OUTPUT] [INPUT]"
	echo ""
	echo "Arguments:"
	echo "  DEVICE    - Device (default: GPU). Supported: CPU, GPU, NPU"
	echo "  OUTPUT    - Output type (default: file). Supported: file, display, fps, json, display-and-json"
	echo "  INPUT     - Input source (default: Pexels video URL)"
	echo ""
	exit 0
fi

DETECT_MODEL="centerface"
DETECT_MODEL_PATH="$MODELS_PATH/public/$DETECT_MODEL/FP32/$DETECT_MODEL.xml"

if [ ! -f "$DETECT_MODEL_PATH" ]; then
	echo "Model not found: $DETECT_MODEL_PATH" >&2
	exit 1
fi

CLASSIFY_MODEL="hsemotion"
CLASSIFY_MODEL_PATH="$MODELS_PATH/public/$CLASSIFY_MODEL/FP16/$CLASSIFY_MODEL.xml"

if [ ! -f "$CLASSIFY_MODEL_PATH" ]; then
	echo "Model not found: $CLASSIFY_MODEL_PATH" >&2
	exit 1
fi

BASE_DIR=$(realpath "$(dirname "$0")")
BUILD_DIR=$BASE_DIR/build

if [ -d "${BUILD_DIR}" ]; then
	rm -r "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

export PKG_CONFIG_PATH=/opt/intel/dlstreamer/gstreamer/lib/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/lib64/pkgconfig:/usr/local/lib/pkgconfig:PKG_CONFIG_PATH

if command -v cmake >/dev/null 2>&1; then
	cmake "${BASE_DIR}"
elif command -v cmake3 >/dev/null 2>&1; then
	cmake3 "${BASE_DIR}"
else
	echo "Error: Neither cmake nor cmake3 is installed." >&2
	exit 1
fi

make -j "$(nproc)"

cd - 1>/dev/null

CUSTOM_POSTPROC_LIB=$(realpath "$BUILD_DIR/libcustom_postproc_classify.so")
if [ ! -f "$CUSTOM_POSTPROC_LIB" ]; then
	echo "Error: Custom post-processing library not found: $CUSTOM_POSTPROC_LIB" >&2
	exit 1
fi

if [[ "$INPUT" == "/dev/video"* ]]; then
	SOURCE_ELEMENT="v4l2src device=${INPUT}"
elif [[ "$INPUT" == *"://"* ]]; then
	SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${INPUT}"
else
	SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

DECODE_ELEMENT="! decodebin3 !"

if [[ "$OUTPUT" == "file" ]]; then
	FILE=$(basename "${INPUT%.*}")
	OUTPUT_FILE="custom_postproc_classify_${DETECT_MODEL}_${CLASSIFY_MODEL}_${FILE}_${DEVICE}.mp4"
	if [ -f "$OUTPUT_FILE" ]; then
		rm "$OUTPUT_FILE"
	fi
	if gst-inspect-1.0 va | grep -q vah264enc; then
		ENCODER="vah264enc"
	elif gst-inspect-1.0 va | grep -q vah264lpenc; then
		ENCODER="vah264lpenc"
	else
		echo "Error - VA-API H.264 encoder not found."
		exit 1
	fi
	SINK_ELEMENT="vapostproc ! gvawatermark ! gvafpscounter ! ${ENCODER} ! h264parse ! mp4mux ! filesink location=$OUTPUT_FILE"
elif [[ "$OUTPUT" == "display" ]] || [[ -z $OUTPUT ]]; then
	SINK_ELEMENT="vapostproc ! gvawatermark ! videoconvertscale ! gvafpscounter ! autovideosink sync=false"
elif [[ "$OUTPUT" == "fps" ]]; then
	SINK_ELEMENT="gvafpscounter ! fakesink async=false"
elif [[ "$OUTPUT" == "json" ]]; then
	OUTPUT_FILE="output.json"
	if [ -f "$OUTPUT_FILE" ]; then
		rm "$OUTPUT_FILE"
	fi
	SINK_ELEMENT="gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=$OUTPUT_FILE ! fakesink async=false"
elif [[ "$OUTPUT" == "display-and-json" ]]; then
	OUTPUT_FILE="output.json"
	if [ -f "$OUTPUT_FILE" ]; then
		rm "$OUTPUT_FILE"
	fi
	SINK_ELEMENT="vapostproc ! gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=$OUTPUT_FILE ! videoconvert ! gvafpscounter ! autovideosink sync=false"
else
	echo Error wrong value for SINK_ELEMENT parameter
	echo Valid values: "file" - render to file, "display" - render to screen, "fps" - print FPS, "json" - write to json file, "display-and-json" - render to screen and write to json file
	exit 1
fi

PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT $DECODE_ELEMENT \
gvadetect model=$DETECT_MODEL_PATH device=$DEVICE pre-process-backend=opencv ! queue ! \
gvaclassify custom-postproc-lib=$CUSTOM_POSTPROC_LIB model=$CLASSIFY_MODEL_PATH device=$DEVICE pre-process-backend=opencv ! queue ! \
$SINK_ELEMENT"

echo "${PIPELINE}"
$PIPELINE
