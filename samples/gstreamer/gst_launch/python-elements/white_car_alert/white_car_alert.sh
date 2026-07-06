#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

# --- Command-line parameters ---
INPUT=${1:-https://github.com/intel-iot-devkit/sample-videos/raw/master/car-detection.mp4}
DEVICE=${2:-GPU}   # Device for detection and classification, e.g. CPU, GPU, GPU.0
OUTPUT=${3:-file}  # Supported values: file, display, fps
COLOR=${4:-white}  # Vehicle color to alert on: white, gray, yellow, red, green, blue, black

SCRIPTDIR="$(dirname "$(realpath "$0")")"
DLSTREAMER_DIR=${DLSTREAMER_DIR:-$(realpath "${SCRIPTDIR}/../../../../..")}
RESULTS_DIR=${RESULTS_DIR:-.}

# --- Models ---
DETECTION_MODEL=${DETECTION_MODEL:-${MODELS_PATH}/public/yolo11n/FP16/yolo11n.xml}
VEHICLE_MODEL=${VEHICLE_MODEL:-${MODELS_PATH}/intel/vehicle-attributes-recognition-barrier-0039/FP16/vehicle-attributes-recognition-barrier-0039.xml}
VEHICLE_MODEL_PROC=${VEHICLE_MODEL_PROC:-${DLSTREAMER_DIR}/samples/gstreamer/model_proc/intel/vehicle-attributes-recognition-barrier-0039.json}

if [[ ! -f ${DETECTION_MODEL} ]]; then
  echo "Error - detection model not found: ${DETECTION_MODEL}"
  echo "Download public models first, e.g.: ${DLSTREAMER_DIR}/samples/download_public_models.sh yolo11n"
  exit 1
fi
if [[ ! -f ${VEHICLE_MODEL} ]]; then
  echo "Error - classification model not found: ${VEHICLE_MODEL}"
  echo "Download OMZ models first, e.g.: ${DLSTREAMER_DIR}/samples/download_omz_models.sh"
  exit 1
fi

# --- Source element ---
if [[ ${INPUT} == "/dev/video"* ]]; then
  SOURCE_ELEMENT="v4l2src device=${INPUT}"
elif [[ ${INPUT} == *"://"* ]]; then
  SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${INPUT}"
else
  SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

# --- Detection pre-process backend (GPU zero-copy when available) ---
if [[ ${DEVICE} == "GPU"* ]]; then
  DETECT_PREPROC="pre-process-backend=va-surface-sharing"
else
  DETECT_PREPROC=""
fi

# --- Sink element ---
if [[ ${OUTPUT} == "display" ]]; then
  SINK_ELEMENT="vapostproc ! gvawatermark ! videoconvert ! gvafpscounter ! autovideosink sync=false"
elif [[ ${OUTPUT} == "fps" ]]; then
  SINK_ELEMENT="gvafpscounter ! fakesink async=false"
elif [[ ${OUTPUT} == "file" ]]; then
  FILE="$(basename "${INPUT%.*}")"
  OUTPUT_FILE="${RESULTS_DIR}/white_car_alert_${FILE}_${DEVICE}.mp4"
  rm -f "${OUTPUT_FILE}"
  if gst-inspect-1.0 va | grep -q vah264enc; then
    ENCODER="vah264enc"
  elif gst-inspect-1.0 va | grep -q vah264lpenc; then
    ENCODER="vah264lpenc"
  else
    echo "Error - VA-API H.264 encoder not found."
    exit 1
  fi
  SINK_ELEMENT="vapostproc ! gvawatermark ! gvafpscounter ! ${ENCODER} ! h264parse ! mp4mux ! filesink location=${OUTPUT_FILE}"
else
  echo "Error - wrong value for OUTPUT parameter"
  echo "Valid values: file - render to file, display - render to screen, fps - print FPS"
  exit 1
fi

echo "Running sample with the following parameters:"
echo "GST_PLUGIN_PATH=${GST_PLUGIN_PATH}"

PIPELINE="gst-launch-1.0 ${SOURCE_ELEMENT} ! decodebin3 ! \
gvadetect model=${DETECTION_MODEL} device=${DEVICE} ${DETECT_PREPROC} ! queue ! \
gvaclassify model=${VEHICLE_MODEL} model-proc=${VEHICLE_MODEL_PROC} device=${DEVICE} pre-process-backend=opencv ! queue ! \
gvawhitecaralert_py color=${COLOR} ! \
${SINK_ELEMENT}"

echo "${PIPELINE}"
GST_PLUGIN_PATH="${GST_PLUGIN_PATH}:${SCRIPTDIR}/plugins" \
  ${PIPELINE}
