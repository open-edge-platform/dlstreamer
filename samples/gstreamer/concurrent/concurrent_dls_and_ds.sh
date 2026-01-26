#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

declare -A DLSTREAMER_PIPELINES
declare -A DEEPSTREAM_PIPELINES

# Get arguments
INPUT="$1"
PIPELINE="$2"
OUTPUT="$3"

if [[ ${OUTPUT} =~ \.mp4 ]]; then
    OUTPUT="${OUTPUT%.*}"
fi

# Check if input is rtsp or uri or file
if [[ ${INPUT} =~ 'rtsp://' ]]; then
    SOURCE="rtspsrc location=${INPUT}"
elif [[ ${INPUT} =~ 'https://' ]]; then
    SOURCE="urisourcebin buffer-size=4096 uri=${INPUT}"
else
    SOURCE="filesrc location=/working_dir/${INPUT}"
fi

# Definition of pipelines
DLSTREAMER_PIPELINES[LPR]="gst-launch-1.0 ${SOURCE} ! decodebin3 ! vapostproc ! video/x-raw\(memory:VAMemory\) ! queue \
! gvadetect model=/working_dir/public/yolov8_license_plate_detector/FP32/yolov8_license_plate_detector.xml \
device=GPU pre-process-backend=va ! queue ! videoconvert ! \
gvaclassify model=/working_dir/public/ch_PP-OCRv4_rec_infer/FP32/ch_PP-OCRv4_rec_infer.xml device=GPU pre-process-backend=va \
! queue ! vapostproc ! gvawatermark ! gvafpscounter ! vah264enc bitrate=2000 ! h264parse ! mp4mux ! filesink location=/working_dir/${OUTPUT}_dls.mp4"


DEEPSTREAM_PIPELINES[LPR]="gst-launch-1.0 ${SOURCE} ! qtdemux ! h264parse ! nvv4l2decoder ! m.sink_0 nvstreammux \
name=m batch-size=1 width=1920 height=1080 batched-push-timeout=40000 ! queue ! nvvideoconvert \
! video/x-raw\(memory:NVMM\),format=RGBA ! nvinfer \
config-file-path=/working_dir/deepstream_tao_apps/configs/nvinfer/trafficcamnet_tao/pgie_trafficcamnet_config.txt \
unique-id=1 ! queue ! nvinfer \
config-file-path=/working_dir/deepstream_tao_apps/configs/nvinfer/LPD_us_tao/sgie_lpd_DetectNet2_us.txt unique-id=2 \
! queue ! nvinfer config-file-path=/working_dir/deepstream_tao_apps/configs/nvinfer/lpr_us_tao/sgie_lpr_us_config.txt \
unique-id=3 ! queue ! nvdsosd display-text=1 display-bbox=1 display-mask=0 process-mode=1 ! nvvideoconvert \
! video/x-raw\(memory:NVMM\),format=NV12 ! nvv4l2h264enc bitrate=2000000 ! h264parse ! qtmux \
! filesink location=/working_dir/${OUTPUT}_ds.mp4 sync=false"

# Check if pipeline is valid
if [[ ! ${DLSTREAMER_PIPELINES[${PIPELINE}]} || ! ${DEEPSTREAM_PIPELINES[${PIPELINE}]} ]]; then
    printf 'Pipeline %s not found.\n' "${PIPELINE}"
    printf 'Available pipelines: '
    for key in "${!DLSTREAMER_PIPELINES[@]}"; do
        printf '%s ' "$key"
    done
    printf '\n'
    exit 1
fi

# Check if there is /dev/dri folder to run on GPU
if [[ -e "/dev/dri" ]]; then
  DEVICE_DRI="--device /dev/dri --group-add $(stat -c "%g" /dev/dri/render* | head -1)"
fi

# Check if there is /dev/accel folder to run on NPU
if [[ -e "/dev/accel" ]]; then
  DEVICE_ACCEL="--device /dev/accel --group-add $(stat -c "%g" /dev/accel/accel* | head -1)"
fi

# Variable for running commands from DL Streamer Docker
DLSTREAMER_DOCKER="docker run -i --rm -v ${PWD}:/working_dir ${DEVICE_DRI} ${DEVICE_ACCEL} \
-v ~/.Xauthority:/root/.Xauthority  -v /tmp/.X11-unix/:/tmp/.X11-unix/  -e DISPLAY=$DISPLAY  -v /dev/bus/usb:/dev/bus/usb \
--env ZE_ENABLE_ALT_DRIVERS=libze_intel_npu.so \
--env MODELS_PATH=/working_dir \
intel/dlstreamer:2025.2.0-ubuntu24 /bin/bash -c"

DEEPSTREAM_SETUP_LPR=$(cat <<EOF
if [[ -e "/working_dir/deepstream_tao_apps" ]]; then
  exit 0
fi

git clone https://github.com/NVIDIA-AI-IOT/deepstream_tao_apps.git

set -e

cd /working_dir/deepstream_tao_apps
mkdir -p ./models/trafficcamnet
cd ./models/trafficcamnet
wget --no-check-certificate --content-disposition 'https://api.ngc.nvidia.com/v2/models/org/nvidia/team/tao/trafficcamnet/pruned_onnx_v1.0.4/files?redirect=true&path=resnet18_trafficcamnet_pruned.onnx' -O resnet18_trafficcamnet_pruned.onnx
wget --no-check-certificate --content-disposition 'https://api.ngc.nvidia.com/v2/models/org/nvidia/team/tao/trafficcamnet/pruned_onnx_v1.0.4/files?redirect=true&path=resnet18_trafficcamnet_pruned_int8.txt' -O resnet18_trafficcamnet_pruned_int8.txt

cd /working_dir/deepstream_tao_apps
mkdir -p ./models/LPD_us
cd ./models/LPD_us
wget --no-check-certificate --content-disposition 'https://api.ngc.nvidia.com/v2/models/org/nvidia/team/tao/lpdnet/pruned_v2.3.1/files?redirect=true&path=LPDNet_usa_pruned_tao5.onnx' -O LPDNet_usa_pruned_tao5.onnx
wget --no-check-certificate --content-disposition 'https://api.ngc.nvidia.com/v2/models/org/nvidia/team/tao/lpdnet/pruned_v2.3.1/files?redirect=true&path=usa_cal_10.1.0.bin' -O usa_cal_10.1.0.bin
wget --no-check-certificate https://api.ngc.nvidia.com/v2/models/nvidia/tao/lpdnet/versions/pruned_v1.0/files/usa_lpd_label.txt

cd /working_dir/deepstream_tao_apps
mkdir -p ./models/LPR_us
cd ./models/LPR_us
wget --no-check-certificate --content-disposition 'https://api.ngc.nvidia.com/v2/models/org/nvidia/team/tao/lprnet/deployable_onnx_v1.1/files?redirect=true&path=us_lprnet_baseline18_deployable.onnx' -O us_lprnet_baseline18_deployable.onnx
touch labels_us.txt


cd /working_dir/deepstream_tao_apps/apps/tao_others/deepstream_lpr_app/nvinfer_custom_lpr_parser/

make

cp /working_dir/deepstream_tao_apps/apps/tao_others/deepstream_lpr_app/dict_us.txt /working_dir/dict.txt

EOF
)

DEEPSTREAM_DOCKER="docker run -i --rm --network=host --gpus all -e DISPLAY=$DISPLAY --device /dev/snd -v /tmp/.X11-unix/:/tmp/.X11-unix -v ${PWD}:/working_dir -w /working_dir nvcr.io/nvidia/deepstream:8.0-samples-multiarch /bin/bash -c"

# Check if there are models in current directory and download if necessary
if [[ ! -e "${PWD}/public/yolov8_license_plate_detector" ]]; then
    printf 'Downloading models....\n'
    eval "${DLSTREAMER_DOCKER}" + '"/opt/intel/dlstreamer/samples/download_public_models.sh yolov8_license_plate_detector,ch_PP-OCRv4_rec_infer"'
fi

# Check for Intel hardware
INTEL_GPU=$(lspci -nn | grep -E 'VGA|3D|Display' | grep -i "Intel")
NVIDIA_GPU=$(lspci -nn | grep -E 'VGA|3D|Display' | grep -i "NVIDIA")
INTEL_CPU=$(lscpu | grep -i "Intel")

print_intel_detected() {
    local HARDWARE="$1"
    printf -- "---------------------------------------\n Intel %s detected. \
Using DL Streamer\n---------------------------------------\n\n" "${HARDWARE}"
}

print_nvidia_detected() {
    printf -- "----------------------------------------\n NVIDIA GPU detected. \
Using DL Streamer\n----------------------------------------\n\n"
}

eval_dlstreamer_pipeline() {
    printf 'PIPELINE:\n%s\n\n' "${DLSTREAMER_PIPELINES[${PIPELINE}]}"
    eval "${DLSTREAMER_DOCKER}" + "\"${DLSTREAMER_PIPELINES[${PIPELINE}]}\"" &
}

eval_deepstream_pipeline() {
    printf 'PIPELINE:\n%s\n\n' "${DEEPSTREAM_PIPELINES[${PIPELINE}]}"
    eval "${DEEPSTREAM_DOCKER}" + "\"${DEEPSTREAM_SETUP_LPR}\""
    eval "${DEEPSTREAM_DOCKER}" + "\"${DEEPSTREAM_PIPELINES[${PIPELINE}]}\"" &
}

replace_in_dlstreamer_pipeline() {
    local FROM="$1"
    local TO="$2"
    DLSTREAMER_PIPELINES[${PIPELINE}]=${DLSTREAMER_PIPELINES[${PIPELINE}]//"${FROM}"/"${TO}"}
}

# Run pipeline
if [[ -n "${NVIDIA_GPU}" && -n "${INTEL_GPU}" ]]; then
    print_nvidia_detected
    print_intel_detected "GPU"
    eval_dlstreamer_pipeline
    eval_deepstream_pipeline
elif [[ -n "${NVIDIA_GPU}" && -e "/dev/accel" ]]; then
    print_nvidia_detected
    print_intel_detected "NPU"
    replace_in_dlstreamer_pipeline "GPU" "NPU"
    eval_dlstreamer_pipeline
    eval_deepstream_pipeline
elif [[ -n "${NVIDIA_GPU}" && -n "${INTEL_CPU}" ]]; then
    print_nvidia_detected
    print_intel_detected "CPU"
    replace_in_dlstreamer_pipeline "GPU" "CPU"
    replace_in_dlstreamer_pipeline "vapostproc !" ""
    replace_in_dlstreamer_pipeline "pre-process-backend=va" ""
    replace_in_dlstreamer_pipeline "video/x-raw\\(memory:VAMemory\) !" ""
    replace_in_dlstreamer_pipeline "vah264enc bitrate=2000" "openh264enc bitrate=2000000"
    eval_dlstreamer_pipeline
    eval_deepstream_pipeline
elif [[ -n "${INTEL_GPU}" ]]; then
    print_intel_detected "GPU"
    eval_dlstreamer_pipeline
elif [[ -n "${NVIDIA_GPU}" ]]; then
    print_nvidia_detected
    eval_deepstream_pipeline
elif [[ -e "/dev/accel" ]]; then
    print_intel_detected "NPU"
    replace_in_dlstreamer_pipeline "GPU" "NPU"
    eval_dlstreamer_pipeline
elif [[ -n "${INTEL_CPU}" ]]; then
    print_intel_detected "CPU"
    replace_in_dlstreamer_pipeline "GPU" "CPU"
    replace_in_dlstreamer_pipeline "vapostproc !" ""
    replace_in_dlstreamer_pipeline "pre-process-backend=va" ""
    replace_in_dlstreamer_pipeline "video/x-raw\\(memory:VAMemory\) !" ""
    replace_in_dlstreamer_pipeline "vah264enc bitrate=2000" "openh264enc bitrate=2000000"
    eval_dlstreamer_pipeline
fi

# wait because of evals with &
wait
