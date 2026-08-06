#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# ==============================================================================
# Pipeline builders for v2 (multi-source single gst-launch-1.0 process).
#
# DL Streamer: N independent source branches, each with shared model instances
#              (model-instance-id) and individual gvafpscounter + fakesink.
# DeepStream:  N source branches feeding a single nvstreammux with batch-size=N,
#              followed by shared inference chain + fakesink.

# Build a DL Streamer pipeline with N branches — no encode (fakesink output).
build_dls_pipeline_no_encode() {
    local n="$1"
    local pipeline="gst-launch-1.0"
    local i
    for i in $(seq 1 "$n"); do
        pipeline+=" ${SOURCE_INTEL} ! decodebin3 ! vapostproc ! video/x-raw\(memory:VAMemory\) ! queue \
! gvadetect model=/working_dir/public/yolov8_license_plate_detector/FP32/yolov8_license_plate_detector.xml \
device=${INTEL_OV_DEVICE} pre-process-backend=va model-instance-id=dls_det ! queue ! \
gvaclassify model=/working_dir/public/ch_PP-OCRv4_rec_infer/FP32/ch_PP-OCRv4_rec_infer.xml \
device=${INTEL_OV_DEVICE} pre-process-backend=va model-instance-id=dls_ocr ! \
queue ! gvafpscounter name=fpsctr${i} ! fakesink sync=false"
    done
    printf '%s' "$pipeline"
}

# Build a DeepStream pipeline with N sources — no encode (fakesink output).
build_ds_pipeline_no_encode() {
    local n="$1"
    local pipeline="gst-launch-1.0"
    local i
    for i in $(seq 1 "$n"); do
        pipeline+=" ${SOURCE_NVIDIA} ! qtdemux ! h264parse ! nvv4l2decoder ! m.sink_$((i - 1))"
    done
    pipeline+=" nvstreammux name=m batch-size=${n} width=1920 height=1080 batched-push-timeout=40000 \
! nvdslogger fps-measurement-interval-sec=1 ! queue ! nvvideoconvert \
! video/x-raw\(memory:NVMM\),format=RGBA ! nvinfer \
config-file-path=/working_dir/deepstream_tao_apps/configs/nvinfer/trafficcamnet_tao/pgie_trafficcamnet_config.txt \
unique-id=1 ! queue ! nvinfer \
config-file-path=/working_dir/deepstream_tao_apps/configs/nvinfer/LPD_us_tao/sgie_lpd_DetectNet2_us.txt unique-id=2 \
! queue ! nvinfer config-file-path=/working_dir/deepstream_tao_apps/configs/nvinfer/lpr_us_tao/sgie_lpr_us_config.txt \
unique-id=3 ! queue ! fakesink sync=false"
    printf '%s' "$pipeline"
}


# ==============================================================================
# Functions:

# Just welcome message
welcome(){
    clear
    printf "========================================\n"
    printf "= Copyright (C) 2026 Intel Corporation =\n"
    printf "=     SPDX-License-Identifier: MIT     =\n"
    printf "========================================\n"
    printf "\n"
    printf "Coexistence Benchmark v2:\n"
    printf "\tDetermines the maximum number of concurrent streams\n"
    printf "\tprocessed in a single gst-launch-1.0 process per platform.\n"
    printf "\n"
} # welcome


# ==============================================================================
# Just print how to use this script:
usage(){
    printf "Usage:\n"
    printf "\t coexistance_benchmark.sh <INPUT_INTEL> <INPUT_NVIDIA> LPR [--dls-only|--ds-only]\n";
    printf "\n"
    printf "Arguments:\n"
    printf "\t INPUT_INTEL   Input video file/stream for Intel platform (DL Streamer)\n"
    printf "\t INPUT_NVIDIA  Input video file/stream for NVIDIA platform (DeepStream)\n"
    printf "\t LPR           Pipeline mode (only LPR is supported)\n"
    printf "\n"
    printf "Options:\n"
    printf "\t --dls-only                 Run benchmark only on Intel GPU/NPU/CPU (DL Streamer)\n";
    printf "\t --ds-only                  Run benchmark only on NVIDIA GPU (DeepStream)\n";
    printf "\t --dls-fps-threshold=N      Minimum acceptable FPS for DL Streamer (default: 20)\n"
    printf "\t --ds-fps-threshold=N       Minimum acceptable FPS for DeepStream (default: 230)\n"
    printf "\t --dls-streams=N            Run exactly N streams on DL Streamer (skip benchmark loop)\n"
    printf "\t --ds-streams=N             Run exactly N streams on DeepStream (skip benchmark loop)\n"
    printf "\t (default: fakesink output, run on both platforms, benchmark mode)\n"
    printf "\n"
    printf "Notes:\n"
    printf "\t In v2 each round runs ONE docker per platform containing all N streams\n"
    printf "\t in a single gst-launch-1.0 command with fakesink output.\n"
}


# ==============================================================================
# Validate if provided input file exists:
validate_input_file(){
printf "Validate input file...\n"
if [ -f "$1" ]; then
    printf "\tSuccess: Input file %s found.\n" "$1"
else
    printf "\tError: Input file %s not found!\n" "$1"
    ERROR_CODE=1
fi
}

# ==============================================================================
# This sample supports only LPR case, so let's check if user is aware of that:
validate_LPR(){
printf "Validate mode...\n"
if [ "${1^^}" = "LPR" ]; then
    printf "\tLPR mode detected.\n"
else
    printf "\tMode %s not supported. Expected value: LPR\n" "$2"
    ERROR_CODE=1
fi
}

# ==============================================================================
validate_input_arguments(){
    # Validate required arguments:
    if [ -z "$1" ] || [ -z "$2" ] || [ -z "$3" ]; then
        printf "Incorrect arguments.\n"
        ERROR_CODE=1
        return 1
    fi

    # Validate Intel input source:
    printf "Validate Deep Learning Streamer input source...\n"
    if [[ "$1" =~ ^rtsp:// ]] || [[ "$1" =~ ^https:// ]]; then
        printf "\tSuccess: Input URL %s accepted.\n" "$1"
    elif [ -f "$1" ]; then
        printf "\tSuccess: Input file %s found.\n" "$1"
    else
        printf "\tError: Deep Learning Streamer input %s is not a valid file or URL!\n" "$1"
        ERROR_CODE=1
    fi
    if [ $ERROR_CODE -eq 1 ]; then
        return 1
    fi

    # Validate DeepStream input source:
    printf "Validate DeepStream input source...\n"
    if [[ "$2" =~ ^rtsp:// ]] || [[ "$2" =~ ^https:// ]]; then
        printf "\tSuccess: Input URL %s accepted.\n" "$2"
    elif [ -f "$2" ]; then
        printf "\tSuccess: Input file %s found.\n" "$2"
    else
        printf "\tError: DeepStream input %s is not a valid file or URL!\n" "$2"
        ERROR_CODE=1
    fi
    if [ $ERROR_CODE -eq 1 ]; then
        return 1
    fi

    # Validate mode:
    validate_LPR "$3"
    if [ $ERROR_CODE -eq 1 ]; then
        usage
        return 1
    fi
}
