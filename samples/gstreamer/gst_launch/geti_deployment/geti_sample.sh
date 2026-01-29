#!/bin/bash
# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# This sample refers to a video file by Rihard-Clement-Ciprian Diac via Pexels
# (https://www.pexels.com)
# ==============================================================================
set -e

if [ -z "${MODELS_PATH:-}" ]; then
  echo "Error: MODELS_PATH is not set." >&2 
  exit 1
else 
  echo "MODELS_PATH: $MODELS_PATH"
fi

# List help message
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: $0 [MODEL_TYPE] [MODEL_PATH] [DEVICE] [PREPROC_BACKEND] [INPUT] [OUTPUT]"
  echo ""
  echo "Arguments:"
  echo "  MODEL_TYPE       - Model type (default: detection). Supported: rotated-detection, instance-segmentation, detection, geti-detection, classification, geti-classification-single, geti-classification-multi, geti-obb, geti-segmentation, anomaly-detection"
  echo "  MODEL_PATH       - Path to the model XML file relative to MODELS_PATH (default: /home/path/to/your/model.xml)"
  echo "  DEVICE           - Device (default: CPU). Supported: CPU, GPU, NPU"
  echo "  PREPROC_BACKEND  - Preprocessing backend (default: opencv for CPU, va-surface-sharing for GPU, va for NPU). Supported: ie, opencv, va, va-surface-sharing"
  echo "  INPUT            - Input source (default: Pexels video URL)"
  echo "  OUTPUT           - Output type (default: file). Supported: file, display, fps, json, display-and-json"
  echo ""
  exit 0
fi

# Default values for parameters
# MODEL_TYPE can be rotated-detection, instance-segmentation, detection, geti-detection, classification, geti-obb, geti-segmentation, geti-classification-single, geti-classification-multi,anomaly-detection
MODEL_TYPE=${1:-detection}
MODEL_PATH=${2:-/home/path/to/your/model.xml}
# Supported values: CPU, GPU, NPU
DEVICE=${3:-CPU}
# PREPROC_BACKEND can be ie/opencv for CPU or va/va-surface-sharing GPU or va for NPU.
PREPROC_BACKEND=${4:-"opencv"}
# INPUT can be a file path, a URL, or a video device (e.g., /dev/video0)
INPUT=${5:-https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4}
# OUTPUT can be file, display, fps, json, display-and-json
OUTPUT=${6:-file}

if [[ ! $MODEL_TYPE =~ ^(rotated-detection|instance-segmentation|detection|geti-detection|classification|geti-classification-single|geti-classification-multi|geti-obb|geti-segmentation|anomaly-detection)$ ]]; then
  echo "Error: Invalid MODEL_TYPE. Supported values: rotated-detection, instance-segmentation, detection, classification, anomaly-detection." >&2
  exit 1
fi
if [[ ! $DEVICE =~ ^(CPU|GPU|NPU)$ ]]; then
  echo "Error: Invalid DEVICE. Supported values: CPU, GPU, NPU." >&2
  exit 1
fi
if [[ ! $PREPROC_BACKEND =~ ^(ie|opencv|va|va-surface-sharing)?$ ]]; then
  echo "Error: Invalid PREPROC_BACKEND. Supported values: ie/opencv for CPU or va/va-surface-sharing GPU or va for NPU" >&2
  exit 1
fi
if [[ ! $OUTPUT =~ ^(file|display|fps|json|display-and-json)?$ ]]; then
  echo "Error: Invalid OUTPUT. Supported values: file, display, fps, json, display-and-json." >&2
  exit 1
fi

FULL_MODEL_PATH="${MODELS_PATH}/${MODEL_PATH}"
echo "FULL_MODEL_PATH: $FULL_MODEL_PATH"

# check if model exists in local directory
if [ ! -f $FULL_MODEL_PATH ]; then
  echo "Model not found: ${FULL_MODEL_PATH}"
  exit 
fi

if [[ $INPUT == "/dev/video"* ]]; then
  SOURCE_ELEMENT="v4l2src device=${INPUT}"
elif [[ $INPUT == *"://"* ]]; then
  SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${INPUT}"
else
  SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

DECODE_ELEMENT="! decodebin3 !"
if [[ $DEVICE == "GPU" ]] || [[ $DEVICE == "NPU" ]]; then
  DECODE_ELEMENT="! decodebin3 ! vapostproc ! video/x-raw(memory:VAMemory) !"
fi

# Validate and set PREPROC_BACKEND based on DEVICE
if [[ "$PREPROC_BACKEND" == "" ]]; then
  PREPROC_BACKEND="opencv" # Default value for CPU
  if [[ "$DEVICE" == "GPU" ]]; then
    PREPROC_BACKEND="va-surface-sharing" # Default value for GPU
  fi
  if [[ "$DEVICE" == "NPU" ]]; then
    PREPROC_BACKEND="va" # Default value for NPU
  fi
else
  if [[ "$PREPROC_BACKEND" == "ie" ]] || [[ "$PREPROC_BACKEND" == "opencv" ]] || [[ "$PREPROC_BACKEND" == "va" ]] || [[ "$PREPROC_BACKEND" == "va-surface-sharing" ]]; then
    PREPROC_BACKEND=${PREPROC_BACKEND}
  else
    echo "Error wrong value for PREPROC_BACKEND parameter. Supported values: ie/opencv for CPU | va/va-surface-sharing/opencv for GPU/NPU".
  fi
fi

INFERENCE_ELEMENT="gvadetect"
if [[ $MODEL_TYPE =~ "classification" ]] || [[ $MODEL_TYPE =~ "anomaly-detection" ]]; then
  INFERENCE_ELEMENT="gvaclassify inference-region=full-frame"
fi

WT_OBB_ELEMENT=" "
if [[ $MODEL_TYPE =~ "rotated-detection" ]]; then
  WT_OBB_ELEMENT=" obb=true "
fi

if [[ $OUTPUT == "file" ]]; then
  FILE="$(basename ${INPUT%.*})"
  rm -f "geti_${FILE}_${MODEL_TYPE}_${DEVICE}.mp4"
  if [[ $(gst-inspect-1.0 va | grep vah264enc) ]]; then
    ENCODER="vah264enc"
  elif [[ $(gst-inspect-1.0 va | grep vah264lpenc) ]]; then
    ENCODER="vah264lpenc"
  else
    echo "Error - VA-API H.264 encoder not found."
    exit
  fi
  SINK_ELEMENT="vapostproc ! gvawatermark${WT_OBB_ELEMENT} ! gvafpscounter ! ${ENCODER} ! h264parse ! mp4mux ! filesink location=geti_${FILE}_${MODEL_TYPE}_${DEVICE}.mp4"
elif [[ $OUTPUT == "display" ]] || [[ -z $OUTPUT ]]; then
  SINK_ELEMENT="vapostproc ! gvawatermark${WT_OBB_ELEMENT} ! videoconvertscale ! gvafpscounter ! autovideosink sync=false"
elif [[ $OUTPUT == "fps" ]]; then
  SINK_ELEMENT="gvafpscounter ! fakesink async=false"
elif [[ $OUTPUT == "json" ]]; then
  rm -f output.json
  SINK_ELEMENT="gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false"
elif [[ $OUTPUT == "display-and-json" ]]; then
  rm -f output.json
  SINK_ELEMENT="vapostproc ! gvawatermark${WT_OBB_ELEMENT}! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! autovideosink sync=false"
else
  echo Error wrong value for SINK_ELEMENT parameter
  echo Valid values: "file" - render to file, "display" - render to screen, "fps" - print FPS, "json" - write to output.json, "display-and-json" - render to screen and write to output.json
  exit
fi

PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT $DECODE_ELEMENT \
$INFERENCE_ELEMENT model=$FULL_MODEL_PATH device=$DEVICE pre-process-backend=$PREPROC_BACKEND ! queue ! \
$SINK_ELEMENT"

echo "${PIPELINE}"
$PIPELINE
