#!/bin/bash
# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# This sample refers to a video file by Rihard-Clement-Ciprian Diac via Pexels
# (https://www.pexels.com)
# ==============================================================================

# Define model proc files
declare -A MODEL_PROC_FILES
MODEL_PROC_FILES["mask_rcnn_inception_resnet_v2_atrous_coco"]="../../model_proc/public/mask-rcnn.json"
MODEL_PROC_FILES["mask_rcnn_resnet50_atrous_coco"]="../../model_proc/public/mask-rcnn.json"

# Allowed choices for arguments
ALLOWED_MODELS=("mask_rcnn_inception_resnet_v2_atrous_coco" "mask_rcnn_resnet50_atrous_coco")
ALLOWED_DEVICES=("CPU" "GPU" "NPU")
ALLOWED_OUTPUTS=("file" "display" "fps" "json" "display-and-json" "jpeg")

# Default values
MODEL="mask_rcnn_inception_resnet_v2_atrous_coco"
DEVICE="CPU"
INPUT="https://videos.pexels.com/video-files/1192116/1192116-sd_640_360_30fps.mp4"
OUTPUT="file"
BENCHMARK_SINK=""
OUTPUT_DIRECTORY=""

show_usage() {
    echo "Usage: $0 [--model MODEL] [--device DEVICE] [--input INPUT] [--output OUTPUT] [--benchmark_sink BENCHMARK_SINK] [--output-directory OUTPUT_DIRECTORY]"
    echo ""
    echo "Arguments:"
    echo "  --model MODEL                     - Model to use (default: mask_rcnn_inception_resnet_v2_atrous_coco). Allowed: ${ALLOWED_MODELS[*]}"
    echo "  --device DEVICE                   - Device to use (default: CPU). Allowed: ${ALLOWED_DEVICES[*]}"
    echo "  --input INPUT                     - Input source (default: Pexels video URL)"
    echo "  --output OUTPUT                   - Output type (default: file). Allowed: ${ALLOWED_OUTPUTS[*]}"
    echo "  --benchmark_sink BENCHMARK_SINK   - Benchmark sink element (default: empty)"
    echo "  --output-directory OUTPUT_DIRECTORY - Directory to save output files (default: current directory)"
    echo "  --help                            - Show this help message"
    echo ""
}

# Function to check if an item is in an array
containsElement () {
  local element match="$1"
  shift
  for element; do
    [[ "$element" == "$match" ]] && return 0
  done
  return 1
}

# Parse arguments
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --model)
            MODEL="$2"
            if ! containsElement "$MODEL" "${ALLOWED_MODELS[@]}"; then
                echo "Invalid model choice. Allowed choices are: ${ALLOWED_MODELS[*]}"
                exit 1
            fi
            shift
            ;;
        --device)
            DEVICE="$2"
            if ! containsElement "$DEVICE" "${ALLOWED_DEVICES[@]}"; then
                echo "Invalid device choice. Allowed choices are: ${ALLOWED_DEVICES[*]}"
                exit 1
            fi
            shift
            ;;
        --input)
            INPUT="$2"
            shift
            ;;
        --benchmark_sink)
            BENCHMARK_SINK="$2"
            shift
            ;;
        --output)
            OUTPUT="$2"
            if ! containsElement "$OUTPUT" "${ALLOWED_OUTPUTS[@]}"; then
                echo "Invalid output choice. Allowed choices are: ${ALLOWED_OUTPUTS[*]}"
                exit 1
            fi
            shift
            ;;
        --output-directory)
            OUTPUT_DIRECTORY="$2"
            shift
            ;;
        --help)
            show_usage
            exit 0
            ;;
        *)
            echo "Unknown parameter passed: $1"
            exit 1
            ;;
    esac
    shift
done

# Check if MODELS_PATH is set
if [ -z "$MODELS_PATH" ]; then
    echo "ERROR - MODELS_PATH is not set." >&2
    exit 1
fi

# Print MODELS_PATH
echo "MODELS_PATH: $MODELS_PATH"

# Save the original directory
orig_dir=$(pwd) || exit

# Change directory to the script's location
cd "$(dirname "$0")" || exit

# Get the model proc file path
MODEL_PROC=${MODEL_PROC_FILES[$MODEL]}
MODEL_PROC=$(realpath "$MODEL_PROC")

if [ ! -f "$MODEL_PROC" ]; then
    echo "ERROR - model-proc file not found: $MODEL_PROC." >&2
    exit 1
fi

# Change back to the previous directory
cd "$orig_dir" || exit

# Construct the model path
MODEL_PATH="${MODELS_PATH}/public/${MODEL}/FP16/${MODEL}.xml"

# Check if model exists in local directory
if [ ! -f "$MODEL_PATH" ]; then
    echo "ERROR - model not found: $MODEL_PATH" >&2
    exit 1
fi

# Determine the source element based on the input
if [[ "$INPUT" == "/dev/video"* ]]; then
    SOURCE_ELEMENT="v4l2src device=${INPUT}"
elif [[ "$INPUT" == *"://"* ]]; then
    SOURCE_ELEMENT="urisourcebin buffer-size=4096 uri=${INPUT}"
else
    SOURCE_ELEMENT="filesrc location=${INPUT}"
fi

# Set decode and preprocessing elements based on the device
DECODE_ELEMENT="! decodebin3 !"
PREPROC_BACKEND="opencv"
if [[ "$DEVICE" == "GPU" ]] || [[ "$DEVICE" == "NPU" ]]; then
    DECODE_ELEMENT+="vapostproc ! video/x-raw(memory:VAMemory) !"
    PREPROC_BACKEND="va"
fi

FILE=$(basename "$INPUT" | cut -d. -f1)

# Determine SINK_ELEMENT based on output argument
declare -A sink_elements
if [[ `uname` != "MINGW64"* ]]; then
    if [[ $(gst-inspect-1.0 va | grep vah264enc) ]]; then
        ENCODER="vah264enc"
    elif [[ $(gst-inspect-1.0 va | grep vah264lpenc) ]]; then
        ENCODER="vah264lpenc"
    else
        echo "Error - VA-API H.264 encoder not found."
        exit
    fi
fi
sink_elements["file"]="vapostproc ! gvawatermark ! gvafpscounter ! ${ENCODER} ! h264parse ! mp4mux ! filesink location=${OUTPUT_DIRECTORY}instance_segmentation_${FILE}_${DEVICE}.mp4"
sink_elements['display']="vapostproc ! gvawatermark ! videoconvertscale ! gvafpscounter ! autovideosink sync=false"
sink_elements['fps']="gvafpscounter ! fakesink sync=false"
sink_elements['json']="gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=${OUTPUT_DIRECTORY}output.json ! fakesink sync=false"
sink_elements['display-and-json']="vapostproc ! gvawatermark ! gvametaconvert add-tensor-data=true ! gvametapublish file-format=json-lines file-path=${OUTPUT_DIRECTORY}instance_segmentation_${FILE}_${DEVICE}.json ! videoconvert ! gvafpscounter ! autovideosink sync=false"
sink_elements["jpeg"]="vapostproc ! gvawatermark ! jpegenc ! multifilesink location=${OUTPUT_DIRECTORY}instance_segmentation_${FILE}_${DEVICE}_%05d.jpeg"
SINK_ELEMENT=${sink_elements[$OUTPUT]}

# Construct the GStreamer pipeline
PIPELINE="gst-launch-1.0 ${SOURCE_ELEMENT} ${BENCHMARK_SINK} ${DECODE_ELEMENT} gvadetect model=${MODEL_PATH} "
if [ -n "$MODEL_PROC" ]; then
    PIPELINE+="model-proc=${MODEL_PROC} "
fi
PIPELINE+="device=${DEVICE} pre-process-backend=${PREPROC_BACKEND} ! queue ! ${SINK_ELEMENT}"

# Get the width of the terminal
term_width=$(tput cols)
# Create a horizontal line with the correct number of asterisks
line=$(printf '%*s' "$term_width" '' | tr ' ' '*')
# Print the message with new lines and dynamic asterisks
echo -e "\n$line"
echo -e "$PIPELINE"
echo -e "$line\n"

$PIPELINE
