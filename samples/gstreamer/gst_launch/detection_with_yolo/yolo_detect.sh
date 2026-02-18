#!/bin/bash
# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# This sample refers to a video file by Rihard-Clement-Ciprian Diac via Pexels
# (https://www.pexels.com)
# ==============================================================================

set -euo pipefail

if [ -z "${MODELS_PATH:-}" ]; then
  echo "Error: MODELS_PATH is not set." >&2
  exit 1
else
  echo "MODELS_PATH: $MODELS_PATH"
fi

# List help message
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
  echo "Usage: $0 [MODEL] [DEVICE] [INPUT] [OUTPUT] [PPBKEND] [PRECISION]"
  echo ""
  echo "Arguments:"
  echo "  MODEL     - Model name (default: yolox_s)"
  echo "            Supported: yolo_all, yolox-tiny, yolox_s, yolov7, yolov8s, yolov8n-obb, yolov8n-seg, yolov9c, yolov10s, yolo11s, yolo11s-obb, yolo11s-seg, yolo11s-pose"
  echo "  DEVICE    - Device (default: GPU). Supported: CPU, GPU, NPU"
  echo "  INPUT     - Input source (default: Pexels video URL)"
  echo "  OUTPUT    - Output type (default: file). Supported: file, display, fps, json, display-and-json"
  echo "  PPBKEND   - Preprocessing backend (default: auto). Supported: ie, opencv, va, va-surface-sharing"
  echo "  PRECISION - Model precision (default: INT8). Supported: INT8, FP32, FP16"
  echo ""
  exit 0
fi 

MODEL=${1:-"yolox_s"}   # Supported values: yolo_all, yolox-tiny, yolox_s, yolov7, yolov8s, yolov8n-obb, yolov8n-seg, yolov9c, yolov10s, yolo11s, yolo11s-obb, yolo11s-seg, yolo11s-pose
DEVICE=${2:-"GPU"}      # Supported values: CPU, GPU, NPU
INPUT=${3:-"https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4"}
OUTPUT=${4:-"file"}     # Supported values: file, display, fps, json, display-and-json
PPBKEND=${5:-""}        # Supported values: ie, opencv, va, va-surface-sharing
PRECISION=${6:-"INT8"}  # Supported values: INT8, FP32, FP16

cd "$(dirname "$0")"

if [[ "$MODEL" == "yolov10s" ]] && [[ "$DEVICE" == "NPU" ]]; then
    echo "Error - No support of Yolov10s for NPU."
    exit 1
fi

if [[ `uname` != "MINGW64"* ]]; then
    GPUS=$(find /dev/dri/ -name "render*")
    if [ -z $GPUS ]; then
        echo "WARN: No GPU detected, switching to CPU"
        DEVICE=CPU
    fi
fi

IE_CONFIG=""
if [[ "$MODEL" == "yolov10s" ]] && [[ "$DEVICE" == "GPU" ]]; then
  IE_CONFIG=" ie-config=GPU_DISABLE_WINOGRAD_CONVOLUTION=YES "
fi

declare -A MODEL_PROC_FILES=(
  ["yolox-tiny"]="../../model_proc/public/yolo-x.json"
  ["yolox_s"]="../../model_proc/public/yolo-x.json"
  ["yolov5s"]="../../model_proc/public/yolo-v7.json"
  ["yolov5su"]="../../model_proc/public/yolo-v8.json"
  ["yolov7"]="../../model_proc/public/yolo-v7.json"
  ["yolov8s"]=""
  ["yolov9c"]=""
  ["yolov8n-obb"]=""
  ["yolov8n-seg"]=""
  ["yolov10s"]=""
  ["yolo11s"]=""
  ["yolo11s-seg"]=""
  ["yolo11s-obb"]=""
  ["yolo11s-pose"]=""
  ["yolo26n"]=""
  ["yolo26s"]=""
  ["yolo26m"]=""
  ["yolo26l"]=""
  ["yolo26x"]=""
  ["yolo26s-obb"]=""
  ["yolo26s-seg"]=""
  ["yolo26s-pose"]=""
)

if ! [[ "${!MODEL_PROC_FILES[*]}" =~ $MODEL ]]; then
  echo "Unsupported model: $MODEL" >&2
  exit 1
fi

MODEL_PROC=""
if ! [ -z ${MODEL_PROC_FILES[$MODEL]} ]; then
  MODEL_PROC=$(realpath "${MODEL_PROC_FILES[$MODEL]}")
fi

cd - 1>/dev/null

if [[ $PRECISION != "INT8" ]] && [[ $PRECISION != "FP32" ]] && [[ $PRECISION != "FP16" ]]; then
  echo "Unsupported model precision: $PRECISION" >&2
  exit 1
fi

MODEL_PATH="${MODELS_PATH}/public/$MODEL/$PRECISION/$MODEL.xml"

# check if model exists in local directory
if [ ! -f $MODEL_PATH ]; then
  echo "Model not found: ${MODEL_PATH}"
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

if [[ "$PPBKEND" == "" ]]; then
  PREPROC_BACKEND="opencv"
  if [[ "$DEVICE" == "GPU" ]]; then
    PREPROC_BACKEND="va-surface-sharing"
  fi
else
  if [[ "$PPBKEND" == "ie" ]] || [[ "$PPBKEND" == "opencv" ]] || [[ "$PPBKEND" == "va" ]] || [[ "$PPBKEND" == "va-surface-sharing" ]]; then
    PREPROC_BACKEND=${PPBKEND}
  else
    echo "Error wrong value for PREPROC_BACKEND parameter. Supported values: ie | opencv | va | va-surface-sharing".
    exit 1
  fi
fi

if [[ "$OUTPUT" == "file" ]]; then
  FILE=$(basename "${INPUT%.*}")
  rm -f "yolo_${FILE}_${MODEL}_${PRECISION}_${DEVICE}.mp4"
  if [[ $(gst-inspect-1.0 va | grep vah264enc) ]]; then
    ENCODER="vah264enc"
  elif [[ $(gst-inspect-1.0 va | grep vah264lpenc) ]]; then
    ENCODER="vah264lpenc"
  else
    echo "Error - VA-API H.264 encoder not found."
    exit 1
  fi
  SINK_ELEMENT="vapostproc ! gvawatermark ! gvafpscounter ! ${ENCODER} ! h264parse ! mp4mux ! filesink location=yolo_${FILE}_${MODEL}_${PRECISION}_${DEVICE}.mp4"
elif [[ "$OUTPUT" == "display" ]] || [[ -z $OUTPUT ]]; then
  SINK_ELEMENT="vapostproc ! gvawatermark ! videoconvertscale ! gvafpscounter ! autovideosink sync=false"
elif [[ "$OUTPUT" == "fps" ]]; then
  SINK_ELEMENT="gvafpscounter ! fakesink async=false"
elif [[ "$OUTPUT" == "json" ]]; then
  rm -f output.json
  SINK_ELEMENT="gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! fakesink async=false"
elif [[ "$OUTPUT" == "display-and-json" ]]; then
  rm -f output.json
  SINK_ELEMENT="vapostproc ! gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=output.json ! videoconvert ! gvafpscounter ! autovideosink sync=false"
else
  echo Error wrong value for SINK_ELEMENT parameter
  echo Valid values: "file" - render to file, "display" - render to screen, "fps" - print FPS, "json" - write to output.json, "display-and-json" - render to screen and write to output.json
  exit 1
fi

PIPELINE="gst-launch-1.0 $SOURCE_ELEMENT $DECODE_ELEMENT \
gvadetect model=$MODEL_PATH"
if [[ -n "$MODEL_PROC" ]]; then
  PIPELINE="$PIPELINE model-proc=$MODEL_PROC"
fi
PIPELINE="$PIPELINE device=$DEVICE pre-process-backend=$PREPROC_BACKEND $IE_CONFIG ! queue ! \
$SINK_ELEMENT"

echo "${PIPELINE}"
$PIPELINE
