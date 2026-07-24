#!/bin/bash
# ==============================================================================
# Copyright (C) 2020-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

if [ -z "${MODELS_PATH:-}" ]; then
  echo "Error: MODELS_PATH is not set." >&2
  exit 1
else
  echo "MODELS_PATH: $MODELS_PATH"
fi

VIDEO_FILE_NAME=${1}
MODEL1_PATH=${2:-"${MODELS_PATH}/public/centerface/FP32/centerface.xml"}
MODEL2_PATH=${3:-"${MODELS_PATH}/public/dima806_face_emotions_image_detection/FP32/dima806_face_emotions_image_detection.xml"}
DECODE_DEVICE=${4:-CPU}     # Supported values: "CPU", "GPU", "AUTO"
INFERENCE_DEVICE=${5:-CPU}  # Supported values: "CPU", "GPU", "AUTO", "MULTI:GPU,CPU"
NUMBER_STREAMS=${6:-1}
NUMBER_PROCESSES=${7:-1}
DECODE_ELEMENT=${8:-"decodebin3"}
INFERENCE1_ELEMENT=${9:-"gvadetect"}
INFERENCE2_ELEMENT=${10:-"gvaclassify"}
SINK_ELEMENT=${11:-"fakesink async=false"}

# check if model exists in local directory
if [ ! -f $MODEL1_PATH ]; then
  echo "Model not found: ${MODEL1_PATH}"
  exit
fi

# check if model exists in local directory
if [ ! -f $MODEL2_PATH ]; then
  echo "Model not found: ${MODEL2_PATH}"
  exit 
fi

if [ -z "${1}" ]; then
  echo "ERROR set path to video"
  echo "Usage : ./benchmark_two_models.sh VIDEO_FILE [MODEL1_PATH] [MODEL2_PATH] [DECODE_DEVICE] [INFERENCE_DEVICE] [NUMBER_STREAMS] [NUMBER_PROCESSES] [DECODE_ELEMENT] [INFERENCE1_ELEMENT] [INFERENCE2_ELEMENT] [SINK_ELEMENT]"
  echo "You can download video with"
  echo "\"curl https://raw.githubusercontent.com/intel-iot-devkit/sample-videos/master/head-pose-face-detection-female-and-male.mp4 --output /path/to/your/video/head-pose-face-detection-female-and-male.mp4\""
  echo "and run sample ./benchmark.sh /path/to/your/video/head-pose-face-detection-female-and-male.mp4"
  exit
fi

if [ "$NUMBER_STREAMS" -lt "$NUMBER_PROCESSES" ]; then
  echo "ERROR: wrong value for NUMBER_STREAMS parameter"
  echo "The number of streams must be greater than or equal to NUMBER_PROCESSES parameter"
  exit
fi

if [ "$INFERENCE_DEVICE" == "CPU" ]; then
    DECODE_ELEMENT+=" ! video/x-raw"
elif [ "$INFERENCE_DEVICE" == "GPU" ]; then
    DECODE_ELEMENT+="! vapostproc"
    DECODE_ELEMENT+=" ! video/x-raw\(memory:VAMemory\)"
fi

if [ "$DECODE_DEVICE" != "AUTO" ] && [ "$DECODE_DEVICE" != "CPU" ] && [ "$DECODE_DEVICE" != "GPU" ]; then
  echo "Incorrect parameter DECODE_DEVICE. Supported values: CPU, GPU, AUTO"
  exit
fi


# Inference parameters
PARAMS=''
if [ "$DECODE_DEVICE" == "GPU" ] && [ "$INFERENCE_DEVICE" == "GPU" ]; then
    PARAMS+="pre-process-backend=va-surface-sharing"
fi
if [ "$DECODE_DEVICE" == "GPU" ] && [ "$INFERENCE_DEVICE" == "CPU" ]; then
    PARAMS+="pre-process-backend=opencv"
fi
if [ "$INFERENCE_DEVICE" == "CPU" ] && [ "$NUMBER_PROCESSES" -gt 1 ]; then # limit number inference threads per process
    CORES=$(nproc)
    THREADS_NUM=$(((CORES + CORES % NUMBER_PROCESSES) / NUMBER_PROCESSES))
    if [ $THREADS_NUM -eq 0 ]; then
      THREADS_NUM=1
    fi
    STREAMS_PER_PROCESS=$((NUMBER_STREAMS / NUMBER_PROCESSES))
    NIREQ=$((2 * STREAMS_PER_PROCESS))
    THROUGHPUT_STREAMS=$((STREAMS_PER_PROCESS))
    PARAMS+="ie-config=INFERENCE_NUM_THREADS=${THREADS_NUM},NUM_STREAMS=${THROUGHPUT_STREAMS},ENABLE_CPU_PINNING=NO nireq=${NIREQ}"
fi

# Pipeline for single stream
PIPELINE=" filesrc location=${VIDEO_FILE_NAME} ! \
${DECODE_ELEMENT} ! \
${INFERENCE1_ELEMENT} model-instance-id=inf0 model=${MODEL1_PATH} device=${INFERENCE_DEVICE} ${PARAMS} ! queue ! \
${INFERENCE2_ELEMENT} model-instance-id=inf1 model=${MODEL2_PATH} device=${INFERENCE_DEVICE} ${PARAMS} ! queue ! \
gvafpscounter ! ${SINK_ELEMENT}"

echo -e "$PIPELINE"

# Launch multiple streams
"$(dirname "$0")"/gst-launch-multi.sh "$PIPELINE" "$NUMBER_STREAMS" "$NUMBER_PROCESSES"
