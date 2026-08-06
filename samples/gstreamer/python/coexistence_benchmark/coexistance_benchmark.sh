#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#
# Benchmark v2: each round launches ONE docker container per platform.
# The single gst-launch-1.0 inside that container grows by one stream branch
# each round, sharing model instances for efficiency.
# Benchmark uses fakesink output to measure pure inference/decode throughput.


# ==============================================================================
# Hardware detection (must be set before sourcing utils.sh):

# Check if there is /dev/dri folder to run on Intel GPU
if [[ -e "/dev/dri" ]]; then
    DEVICE_DRI="--device /dev/dri --group-add $(stat -c "%g" /dev/dri/render* | head -1)"
fi

# Check if there is /dev/accel folder to run on Intel NPU
if [[ -e "/dev/accel" ]]; then
    DEVICE_ACCEL="--device /dev/accel --group-add $(stat -c "%g" /dev/accel/accel* | head -1)"
fi

# Determine GStreamer source elements from input arguments (must be set before sourcing uti./coexistance_benchmark.sh ParkingVideo_long_dls.mp4 ParkingVideo_long_ds.mp4 LPR output_test_2 --dls-streams=1 --ds-streams=5ls.sh)
# INPUT_INTEL ($1)
if [[ "$1" =~ 'rtsp://' ]]; then
    SOURCE_INTEL="rtspsrc location=$1"
    EXTRA_INPUT_VOLUME_INTEL=""
elif [[ "$1" =~ 'https://' ]]; then
    SOURCE_INTEL="urisourcebin buffer-size=4096 uri=$1"
    EXTRA_INPUT_VOLUME_INTEL=""
elif [[ "$1" = /* ]]; then
    INPUT_DIR=$(dirname "$1")
    SOURCE_INTEL="filesrc location=$1"
    EXTRA_INPUT_VOLUME_INTEL="-v ${INPUT_DIR}:${INPUT_DIR}"
else
    SOURCE_INTEL="filesrc location=/working_dir/$1"
    EXTRA_INPUT_VOLUME_INTEL=""
fi

# INPUT_NVIDIA ($2)
if [[ "$2" =~ 'rtsp://' ]]; then
    SOURCE_NVIDIA="rtspsrc location=$2"
    EXTRA_INPUT_VOLUME_NVIDIA=""
elif [[ "$2" =~ 'https://' ]]; then
    SOURCE_NVIDIA="urisourcebin buffer-size=4096 uri=$2"
    EXTRA_INPUT_VOLUME_NVIDIA=""
elif [[ "$2" = /* ]]; then
    INPUT_DIR=$(dirname "$2")
    SOURCE_NVIDIA="filesrc location=$2"
    EXTRA_INPUT_VOLUME_NVIDIA="-v ${INPUT_DIR}:${INPUT_DIR}"
else
    SOURCE_NVIDIA="filesrc location=/working_dir/$2"
    EXTRA_INPUT_VOLUME_NVIDIA=""
fi

# Detect Intel GPU: prefer discrete Arc (dGPU) over integrated (iGPU).
INTEL_RENDER_DEVICE=""
INTEL_OV_DEVICE="GPU"
for _d in /dev/dri/render*; do
    _vendor=$(cat /sys/class/drm/$(basename "$_d")/device/vendor 2>/dev/null)
    if [[ "$_vendor" == "0x8086" ]]; then
        _pci=$(basename "$(readlink /sys/class/drm/$(basename "$_d")/device 2>/dev/null)" 2>/dev/null)
        if [[ "$_pci" =~ ^0000:00:02 ]]; then
            [[ -z "$INTEL_RENDER_DEVICE" ]] && INTEL_RENDER_DEVICE="$_d"
        else
            INTEL_RENDER_DEVICE="$_d"
        fi
    fi
done
printf 'Intel render device: %s  OpenVINO device: %s\n' "${INTEL_RENDER_DEVICE}" "${INTEL_OV_DEVICE}"

if [[ -n "$INTEL_RENDER_DEVICE" ]]; then
    # Keep full /dev/dri (card* nodes required by iHD driver for VA-API init); GST_VA_DRM_DEVICE selects the GPU.
    _dgpu_group=$(stat -c "%g" "$INTEL_RENDER_DEVICE")
    DEVICE_DRI="--device /dev/dri --group-add ${_dgpu_group}"
    printf 'Using DRI device: %s (group %s)\n' "${INTEL_RENDER_DEVICE}" "${_dgpu_group}"
fi

# ==============================================================================
# Docker commands (must be defined AFTER setting EXTRA_INPUT_VOLUME_INTEL/NVIDIA):

declare DLSTREAMER_DOCKER="docker run -i --rm --name benchmark_dls -v ${PWD}:/working_dir ${EXTRA_INPUT_VOLUME_INTEL} ${DEVICE_DRI} ${DEVICE_ACCEL} \
-v ~/.Xauthority:/root/.Xauthority  -v /tmp/.X11-unix/:/tmp/.X11-unix/  -e DISPLAY=$DISPLAY  -v /dev/bus/usb:/dev/bus/usb \
--env MODELS_PATH=/working_dir -e GST_VA_DRM_DEVICE=${INTEL_RENDER_DEVICE} \
intel/dlstreamer:latest /bin/bash -c"

declare DEEPSTREAM_DOCKER="docker run -i --rm --name benchmark_ds --network=host --gpus all -e DISPLAY=$DISPLAY --device /dev/snd \
-v /tmp/.X11-unix/:/tmp/.X11-unix -v ${PWD}:/working_dir ${EXTRA_INPUT_VOLUME_NVIDIA} -w /working_dir \
nvcr.io/nvidia/deepstream:8.0-samples-multiarch /bin/bash -c"

# ==============================================================================
# Imports:
. ./utils.sh

# ==============================================================================
# Variables:
declare _MODE="LPR"
declare ERROR_CODE=0
declare DLS_CONTAINER_NAME="benchmark_dls"
declare DS_CONTAINER_NAME="benchmark_ds"
# Active (possibly unique) container names for the currently running round.
declare DLS_ACTIVE_CONTAINER="benchmark_dls"
declare DS_ACTIVE_CONTAINER="benchmark_ds"
# Loop iteration counters per platform (used as container-name suffix: _1, _2, ...).
declare -i DLS_ROUND_INDEX=0
declare -i DS_ROUND_INDEX=0

# Parse optional platform flags (from $4 onwards: $3=MODE)
RUN_DLS=true
RUN_DS=true
DLS_FPS_THRESHOLD=30
DS_FPS_THRESHOLD=30
DLS_FIXED_STREAMS=""   # empty = benchmark mode; N = run exactly N streams once
DS_FIXED_STREAMS=""    # empty = benchmark mode; N = run exactly N streams once
OPTION_START_INDEX=4

for _arg in "${@:${OPTION_START_INDEX}}"; do
    case "$_arg" in
        --dls-only)               RUN_DS=false                          ;;
        --ds-only)                RUN_DLS=false                           ;;
        --dls-fps-threshold=*)    DLS_FPS_THRESHOLD="${_arg#*=}"            ;;
        --ds-fps-threshold=*)     DS_FPS_THRESHOLD="${_arg#*=}"             ;;
        --dls-streams=*)          DLS_FIXED_STREAMS="${_arg#*=}"            ;;
        --ds-streams=*)           DS_FIXED_STREAMS="${_arg#*=}"             ;;
    esac
done


# ==============================================================================
# Functions:

# Global abort flag — set by SIGINT/SIGTERM to break all loops cleanly.
ABORT=false

# Cleanup: kill docker processes started by this script.
cleanup() {
    [[ -n "${_CLEANED}" ]] && return
    _CLEANED=1
    ABORT=true
    printf "\n\nCleaning up — stopping all containers and processes...\n"
    kill "${LIVE_MONITOR_PID}" 2>/dev/null
    [[ -n "${DLS_PID}" ]] && { kill "${DLS_PID}" 2>/dev/null; wait "${DLS_PID}" 2>/dev/null; }
    [[ -n "${DS_PID}"  ]] && { kill "${DS_PID}"  2>/dev/null; wait "${DS_PID}"  2>/dev/null; }
    for _cid in $(docker ps -q --filter ancestor=intel/dlstreamer:latest); do
        printf "[cleanup] Force-killing container %s (intel/dlstreamer:latest)\n" "${_cid}"
        docker kill "${_cid}" >/dev/null 2>&1
    done
    for _cid in $(docker ps -q --filter ancestor=nvcr.io/nvidia/deepstream:8.0-samples-multiarch); do
        printf "[cleanup] Force-killing container %s (nvcr.io/nvidia/deepstream:8.0-samples-multiarch)\n" "${_cid}"
        docker kill "${_cid}" >/dev/null 2>&1
    done
    # Remove any stopped containers by name pattern (catch stopped containers missed by ancestor filter).
    docker ps -a -q --filter "name=^benchmark_" | xargs -r docker rm -f >/dev/null 2>&1 || true
    [[ -n "${BENCH_TMPDIR}" && -d "${BENCH_TMPDIR}" ]] && rm -rf "${BENCH_TMPDIR}"
    printf "Cleanup done.\n"
}

# Ensure a named docker container does not exist before starting a new round.
ensure_container_absent() {
    local cname="$1"
    if docker container inspect "${cname}" >/dev/null 2>&1; then
        local _cid
        _cid=$(docker container inspect -f '{{.Id}}' "${cname}" 2>/dev/null | head -n 1)
        printf "  [docker] Removing stale container %s (%s)\n" "${cname}" "${_cid:-unknown}"
        docker rm -f "${cname}" >/dev/null 2>&1 || true
    fi
}


# ==============================================================================
# Start: display welcome message and validate input arguments
welcome
if [[ "$#" -gt 0 ]]; then
    if ! validate_input_arguments "$1" "$2" "$3"; then
        usage
        printf "Exiting script...\\n\\n"
        exit 1
    fi
else
    printf "No input arguments provided. Checking model availability first...\n\n"
fi

# ==============================================================================
# Print configured parameters:
if [[ "$#" -gt 0 ]]; then
    printf "Configuration:\n"
    printf "\t Input Intel   : %s\n" "$1"
    printf "\t Input NVIDIA  : %s\n" "$2"
    printf "\t Mode          : %s\n" "$3"
    printf "\t Platforms     : %s\n" "$( [[ "$RUN_DLS" == true && "$RUN_DS" == true ]] && echo "Intel + NVIDIA" || ( [[ "$RUN_DLS" == true ]] && echo "Intel only" || echo "NVIDIA only" ) )"
    printf "\t DLS mode      : %s\n" "$( [[ -n "$DLS_FIXED_STREAMS" ]] && printf "fixed %s stream(s)" "$DLS_FIXED_STREAMS" || printf "benchmark (threshold: %s FPS)" "$DLS_FPS_THRESHOLD" )"
    printf "\t DS  mode      : %s\n" "$( [[ -n "$DS_FIXED_STREAMS"  ]] && printf "fixed %s stream(s)" "$DS_FIXED_STREAMS"  || printf "benchmark (threshold: %s FPS)" "$DS_FPS_THRESHOLD"  )"
    printf "\t Measure time  : %s s\n" "${MEASURE_SECONDS}"
    printf "\n"
fi

# ==============================================================================
# Detect available hardware:
INTEL_GPU=$(lspci -nn | grep -E 'VGA|3D|Display' | grep -i "Intel")
NVIDIA_GPU=$(lspci -nn | grep -E 'VGA|3D|Display' | grep -i "NVIDIA")
INTEL_CPU=$(lscpu | grep -i "Intel")

if [[ "$RUN_DLS" == true && -n "${INTEL_GPU}" ]]; then
    printf '%b' "---------------------------------------\n Intel GPU detected. Using DL Streamer\n---------------------------------------\n\n"
elif [[ "$RUN_DLS" == true && -e "/dev/accel" ]]; then
    printf '%b' "---------------------------------------\n Intel NPU detected. Using DL Streamer\n---------------------------------------\n\n"
elif [[ "$RUN_DLS" == true && -n "${INTEL_CPU}" ]]; then
    printf '%b' "---------------------------------------\n Intel CPU detected. Using DL Streamer\n---------------------------------------\n\n"
fi
if [[ "$RUN_DS" == true && -n "${NVIDIA_GPU}" ]]; then
    printf '%b' "----------------------------------------\n NVIDIA GPU detected. Using DeepStream\n----------------------------------------\n\n"
fi

if [[ -z "${INTEL_GPU}" && -z "${NVIDIA_GPU}" && ! -e "/dev/accel" && -z "${INTEL_CPU}" ]]; then
    if [[ "$#" -eq 0 ]]; then
        printf "Warning: No supported hardware detected. Skipping container-based model download.\n"
    else
        printf "Error: No supported hardware detected.\n"
        exit 1
    fi
fi

# Build human-readable list of active platforms for summary messages
ACTIVE_PLATFORMS=()
if [[ "$RUN_DLS" == true ]]; then
    if [[ -n "${INTEL_GPU}" ]]; then
        ACTIVE_PLATFORMS+=("Intel GPU")
    elif [[ -e "/dev/accel" ]]; then
        ACTIVE_PLATFORMS+=("Intel NPU")
    elif [[ -n "${INTEL_CPU}" ]]; then
        ACTIVE_PLATFORMS+=("Intel CPU")
    fi
fi
if [[ "$RUN_DS" == true && -n "${NVIDIA_GPU}" ]]; then
    ACTIVE_PLATFORMS+=("NVIDIA GPU")
fi
if [[ "${#ACTIVE_PLATFORMS[@]}" -gt 0 ]]; then
    PLATFORM_SCOPE=$(IFS=', '; echo "${ACTIVE_PLATFORMS[*]}")
else
    PLATFORM_SCOPE="detected platform(s)"
fi

# ==============================================================================
# Download DL Streamer models if needed:
if [[ "$RUN_DLS" == true && ( -n "${INTEL_GPU}" || -e "/dev/accel" || -n "${INTEL_CPU}" ) ]]; then
    if [[ ! -e "${PWD}/public/yolov8_license_plate_detector" ]]; then
        printf 'Downloading DL Streamer models....\n'
        eval "${DLSTREAMER_DOCKER/--name ${DLS_CONTAINER_NAME}/--name ${DLS_CONTAINER_NAME}_download} \"/opt/intel/dlstreamer/samples/download_public_models.sh yolov8_license_plate_detector,ch_PP-OCRv4_rec_infer\""
    else
        printf 'DL Streamer models already present, skipping download.\n'
    fi
fi

# ==============================================================================
# Download DeepStream TAO models if needed:
DEEPSTREAM_SETUP_LPR=$(cat <<'SETUP_EOF'
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
SETUP_EOF
)

if [[ "$RUN_DS" == true && -n "${NVIDIA_GPU}" ]]; then
    if [[ ! -e "${PWD}/deepstream_tao_apps" ]]; then
        printf 'Downloading DeepStream TAO models....\n'
        eval "${DEEPSTREAM_DOCKER/--name ${DS_CONTAINER_NAME}/--name ${DS_CONTAINER_NAME}_download} \"${DEEPSTREAM_SETUP_LPR}\""
    else
        printf 'DeepStream TAO models already present, skipping download.\n'
    fi
fi

if [[ "$#" -eq 0 ]]; then
    DLS_MODELS_OK=false
    DS_MODELS_OK=false

    if [[ -d "${PWD}/public/yolov8_license_plate_detector" && -d "${PWD}/public/ch_PP-OCRv4_rec_infer" ]]; then
        DLS_MODELS_OK=true
    fi
    if [[ -d "${PWD}/deepstream_tao_apps" ]]; then
        DS_MODELS_OK=true
    fi

    printf "\nModel availability check:\n"
    printf "\t DL Streamer models: %s\n" "$( [[ "$DLS_MODELS_OK" == true ]] && echo "available" || echo "missing" )"
    printf "\t DeepStream models : %s\n" "$( [[ "$DS_MODELS_OK"  == true ]] && echo "available" || echo "missing" )"

    printf "\nIncorrect arguments.\n"
    usage
    printf "Exiting script...\n\n"
    exit 1
fi

# ==============================================================================
# Benchmark configuration

MEASURE_SECONDS=45
DS_ENGINE_BUILD_GRACE_SECONDS=420
STREAMS=1
KEEP_RUNNING=true

DLS_PID=""
DS_PID=""
LIVE_MONITOR_PID=""
BENCH_TMPDIR=""

trap 'cleanup; exit 130' SIGINT SIGTERM
trap cleanup EXIT

# ==============================================================================
# Helper functions

# Extract average per-stream FPS from a pipeline log file.
# DL Streamer: each gvafpscounter (one per stream branch) prints independently:
#              "FpsCounter(average Xs): total=Y fps, number-streams=1, per-stream=Y fps"
#              Collect all per-stream= values from average lines; take the minimum (slowest stream).
# DeepStream:  nvdslogger prints "**PERF : FPS_N (XX)" per source per interval.
#              Average of ALL parenthesised values = per-source average FPS.
get_avg_fps() {
    local logfile="$1"
    local platform="$2"
    local streams="${3:-0}"  # number of stream branches; limits to final average per counter
    local values
    if [[ "$platform" == "dls" ]]; then
        # Use only the last `streams` values = one final cumulative average per counter (excludes startup transients).
        values=$(grep 'FpsCounter(average' "$logfile" 2>/dev/null \
            | grep -oP 'per-stream=\K[0-9]+\.?[0-9]*' \
            | tail -n "$(( streams > 0 ? streams : 9999 ))")
    else
        values=$(grep 'PERF' "$logfile" 2>/dev/null | grep -oP '\(\K[0-9]+\.?[0-9]*')
    fi
    if [[ -z "$values" ]]; then
        echo "0"
    else
        if [[ "$platform" == "dls" ]]; then
            echo "$values" | awk '{ for (i = 1; i <= NF; i++) { if (min == "" || $i < min) min = $i } } END { printf "%.1f", (min == "" ? 0 : min) }'
        else
            echo "$values" | awk '{ sum += $1; n++ } END { printf "%.1f", (n > 0 ? sum/n : 0) }'
        fi
    fi
}

# Returns 0 (true) if given FPS is strictly below the threshold
fps_below_threshold() {
    awk -v fps="$1" -v thr="${FPS_THRESHOLD}" 'BEGIN { exit (fps + 0 < thr ? 0 : 1) }'
}

# Detect CUDA / GPU memory pressure signatures in a stream log.
is_oom_log() {
    local logfile="$1"
    grep -qiE "CUDA memory is already at|bufferpool resize failed|failed to activate bufferpool|\
Failed to set buffer pool to active|Cuda failure: status=2|Error\(-1\) in buffer allocation|\
cudaErrorMemoryAllocation|Failed to create cuda stream|cudaStreamCreateWithFlags failed|\
out of memory|OutOfMemory|create TRT cuda executionContext failed|defaultAllocator\.cpp::allocate" \
        "$logfile" 2>/dev/null

}

# Detect pipeline stalled in PREROLL and never reaching PLAYING.
is_preroll_stall_log() {
    local logfile="$1"
    grep -qE "Pipeline is PREROLLING|Pipeline is PREROLLED" "$logfile" \
        && ! grep -qE "Setting pipeline to PLAYING|New clock|PERF|FpsCounter" "$logfile"
}

# Detect DeepStream first-run TensorRT engine build in progress.
is_ds_engine_build_log() {
    local logfile="$1"
    grep -qiE "Trying to create engine from model files|deserialize backend context.*failed, try rebuild|Deserialize engine failed because file path" \
        "$logfile" 2>/dev/null
}

# Report DeepStream engine file readiness for PGIE/SGIEs.
ds_engine_state() {
    local path="$1"
    local build_in_progress="${2:-false}"
    if [[ -f "$path" ]]; then
        local size_mb
        size_mb=$(stat -c %s "$path" 2>/dev/null | awk '{ printf "%.1f", $1/1048576 }')
        printf "ready(%sMB)" "${size_mb:-?}"
    elif [[ "$build_in_progress" == "true" ]]; then
        printf "waiting(engine-build)"
    else
        printf "missing"
    fi
}

print_ds_engine_status() {
    local logfile="${1:-}"
    local base="${PWD}/deepstream_tao_apps/models"
    local pgie="${base}/trafficcamnet/resnet18_trafficcamnet_pruned.onnx_b1_gpu0_fp16.engine"
    local lpd="${base}/LPD_us/LPDNet_usa_pruned_tao5.onnx_b16_gpu0_fp16.engine"
    local lpr="${base}/LPR_us/us_lprnet_baseline18_deployable.onnx_b16_gpu0_fp16.engine"
    local build_in_progress="false"

    # Nothing to report once every engine file is already compiled.
    if [[ -f "$pgie" && -f "$lpd" && -f "$lpr" ]]; then
        return 0
    fi

    if [[ -n "$logfile" && -f "$logfile" ]] && is_ds_engine_build_log "$logfile"; then
        build_in_progress="true"
    fi

    printf "  [diag] DS engines: PGIE=%s  LPD=%s  LPR=%s\n" \
        "$(ds_engine_state "$pgie" "$build_in_progress")" \
        "$(ds_engine_state "$lpd" "$build_in_progress")" \
        "$(ds_engine_state "$lpr" "$build_in_progress")"

    # if [[ "$build_in_progress" == "true" ]]; then
    #     printf "  [diag] DeepStream TensorRT engine build in progress; missing files are expected until build completes.\n"
    # fi
}

    # Normalize DeepStream PERF line for human-readable display.
    normalize_ds_perf_line() {
        local line="$1"
        printf '%s\n' "$line" \
        | sed -E 's/\r//g; s/\)([0-9]+)FPS_/\)  FPS_/g; s/[[:space:]]+/ /g; s/^ //; s/ $//'
    }

# Wait until a newly-started pipeline reaches a meaningful running milestone or fails.
# Returns: 0=ok, 1=error/OOM, 2=timeout
wait_for_pipeline_start() {
    local label="$1"
    local pid="$2"
    local logfile="$3"
    local timeout_sec="$4"
    local elapsed=0
    local _fatal_line

    printf "    [sync] Waiting for startup of %s (PID=%s, timeout=%ss)\n" "$label" "$pid" "$timeout_sec"
    while (( elapsed < timeout_sec )); do
        [[ "$ABORT" == true ]] && return 1
        if ! kill -0 "$pid" 2>/dev/null; then
            printf "    [sync] %s exited during startup (PID=%s).\n" "$label" "$pid"
            return 1
        fi
        if [[ -f "$logfile" ]]; then
            if is_oom_log "$logfile"; then
                printf "    [sync] %s OOM detected during startup. Stopping PID=%s.\n" "$label" "$pid"
                kill "$pid" 2>/dev/null
                wait "$pid" 2>/dev/null
                return 1
            fi
            # Milestone: pipeline is actively running
            if grep -qE "Setting pipeline to PLAYING|New clock|PERF|FpsCounter" "$logfile"; then
                printf "    [sync] %s reached startup milestone.\n" "$label"
                return 0
            fi
            # Fatal startup errors (filter known benign scanner warnings)
            _fatal_line=$(grep -iE "ERROR|critical|out of memory|OutOfMemory|\
pipeline doesn't want to preroll|not-negotiated|internal data stream error|\
segmentation fault|aborted|Device '/dev/v4l2-nvenc' failed during initialization|\
Failed to create NvDsInferContext|create TRT cuda executionContext failed" \
                "$logfile" 2>/dev/null \
                | grep -viE "gst-plugin-scanner|Failed to load plugin|deserialize engine from file .* failed|deserialize backend context from engine from file .* failed, try rebuild|Deserialize engine failed because file path|Trying to create engine from model files|\
libva error|iHD_drv_video|DRM_IOCTL_VERSION|vaGetDriverNames|unsupported drm device by media driver" \
                | tail -n 1)
            if [[ -n "${_fatal_line}" ]]; then
                printf "    [sync] %s reported startup error: %s\n" "$label" "${_fatal_line}"
                return 1
            fi
        fi
        # Periodic heartbeat so long startups (e.g. DeepStream TensorRT engine build) show progress.
        if (( elapsed > 0 && elapsed % 5 == 0 )); then
            # printf "    [sync] %s still starting... (%ss/%ss elapsed)\n" "$label" "$elapsed" "$timeout_sec"
            if [[ -f "$logfile" ]]; then
                local _last
                _last=$(grep -aE '.' "$logfile" 2>/dev/null | tail -n 1)
                # [[ -n "$_last" ]] && printf "    [sync] last log: %s\n" "${_last:0:160}"
                if [[ "$label" == ds\ * || "$label" == *DeepStream* ]]; then
                    print_ds_engine_status "$logfile"
                fi
            fi
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    printf "    [sync] %s startup timeout reached (%ss), continuing.\n" "$label" "$timeout_sec"
    return 2
}

# Print diagnostics for a failed/stalled pipeline log.
print_pipeline_diagnostics() {
    local label="$1"
    local pid="$2"
    local logfile="$3"
    local state="$4"

    printf "  [diag] %s state=%s (PID=%s)\n" "$label" "$state" "$pid"
    if [[ ! -f "$logfile" ]]; then
        printf "  [diag] Log file not found: %s\n" "$logfile"
        return
    fi

    printf "  [diag] Last 30 log lines for %s:\n" "$label"
    tail -n 30 "$logfile" 2>/dev/null | sed 's/^/    | /'

    local _err
    _err=$(grep -iE 'error|failed|fatal|cuda|nvinfer|nvbuf|memory|cannot|segmentation|killed' \
        "$logfile" 2>/dev/null | tail -n 10)
    if [[ -n "$_err" ]]; then
        printf "  [diag] Error-like lines:\n"
        printf '%s\n' "$_err" | sed 's/^/    ! /'
    fi

    if is_oom_log "$logfile"; then
        printf "  [cause] GPU/CUDA memory exhaustion detected.\n"
        printf "  [cause] This sets the practical stream limit for current model set and resolution.\n"
    fi

    if is_preroll_stall_log "$logfile"; then
        printf "  [cause] Pipeline stalled in PREROLL — never reached PLAYING/FPS.\n"
        printf "  [cause] Likely GPU resource saturation (memory bandwidth, VA surfaces, or inference queue).\n"
    fi
}


# ==============================================================================
# Benchmark helper: run one platform for one round, return FPS and status.
# Usage: run_one_round <platform> <streams> <logfile> [nowait]
# platform: "dls" or "ds"
# nowait: if set to "nowait", skips the measurement timeout — caller handles waiting.
# Sets global: ROUND_FPS, ROUND_STATUS, ROUND_PID
run_one_round() {
    local platform="$1"
    local streams="$2"
    local logfile="$3"
    local nowait="${4:-}"
    local pipeline pid run_cmd

    ROUND_FPS="0"
    ROUND_STATUS="no-fps"
    ROUND_PID=""

    local sync_timeout=$(( 60 + streams * 15 ))

    if [[ "$platform" == "dls" ]]; then
        DLS_ROUND_INDEX=$(( DLS_ROUND_INDEX + 1 ))
        DLS_ACTIVE_CONTAINER="${DLS_CONTAINER_NAME}_${DLS_ROUND_INDEX}"
        ensure_container_absent "${DLS_ACTIVE_CONTAINER}"
        pipeline=$(build_dls_pipeline_no_encode "$streams")
        printf "  [Intel / DL Streamer] Starting pipeline with %d stream branch(es)\n" "$streams"
        printf "    Pipeline: %s\n\n" "$pipeline"
        run_cmd="${DLSTREAMER_DOCKER/--name ${DLS_CONTAINER_NAME}/--name ${DLS_ACTIVE_CONTAINER}} \"${pipeline}\""
        printf "    Docker command: %s\n\n" "${run_cmd}"
        eval "${run_cmd}" > "$logfile" 2>&1 &
    else
        DS_ROUND_INDEX=$(( DS_ROUND_INDEX + 1 ))
        DS_ACTIVE_CONTAINER="${DS_CONTAINER_NAME}_${DS_ROUND_INDEX}"
        ensure_container_absent "${DS_ACTIVE_CONTAINER}"
        pipeline=$(build_ds_pipeline_no_encode "$streams")
        printf "  [NVIDIA / DeepStream] Starting pipeline with %d source(s)\n" "$streams"
        printf "    Pipeline: %s\n\n" "$pipeline"
        run_cmd="${DEEPSTREAM_DOCKER/--name ${DS_CONTAINER_NAME}/--name ${DS_ACTIVE_CONTAINER}} \"${pipeline}\""
        printf "    Docker command: %s\n\n" "${run_cmd}"
        eval "${run_cmd}" > "$logfile" 2>&1 &
    fi
    pid=$!
    ROUND_PID=$pid
    if [[ "$platform" == "dls" ]]; then
        DLS_PID=$pid
    else
        DS_PID=$pid
    fi

    printf "    PID: %d\n" "$pid"
    wait_for_pipeline_start "${platform} (${streams} streams)" "$pid" "$logfile" "$sync_timeout"
    [[ "$ABORT" == true ]] && return 1

    # warn if process already died
    if ! kill -0 "$pid" 2>/dev/null; then
        printf "  [warn] %s exited before measurement (PID=%s). Last log lines:\n" "$platform" "$pid"
        tail -n 10 "$logfile" 2>/dev/null | sed 's/^/    | /'
    fi

    # live FPS monitor (background) — DLS reads full logfile for combined per-stream view; DS reads new bytes.
    (
        _off=0
        _diag_tick=0
        while true; do
            _alive=1
            kill -0 "$pid" 2>/dev/null || _alive=0
            if [[ -f "$logfile" ]]; then
                _size=$(stat -c %s "$logfile" 2>/dev/null || echo 0)
                if (( _size > _off )); then
                    _new=$(tail -c "+$((_off + 1))" "$logfile" 2>/dev/null)
                    _off=$_size
                    _fps=""
                    _fps_line=""
                    if [[ "$platform" == "dls" ]]; then
                        # Read last `streams` instantaneous values from full log — combines all counters on one line.
                        _fps=$(grep 'FpsCounter(last' "$logfile" 2>/dev/null \
                            | grep -oP 'per-stream=\K[0-9]+\.?[0-9]*' | tail -n "$streams" | paste -sd ',' | sed 's/,/, /g')
                    else
                        _perf_line=$(printf '%s\n' "$_new" | grep 'PERF' | tail -1)
                    fi
                    if [[ -n "$_fps" ]]; then
                        if [[ "$platform" == "dls" ]]; then
                            printf "  [live] %-30s FPS per-stream: (%s)\n" "${platform} (${streams} streams):" "$_fps"
                        else
                            printf "  [live] %-30s %s\n" "${platform} (${streams} streams):" "$(normalize_ds_perf_line "$_perf_line")"
                        fi
                    elif [[ "$platform" == "ds" && -n "$_perf_line" ]]; then
                        printf "  [live] %-30s %s\n" "${platform} (${streams} streams):" "$(normalize_ds_perf_line "$_perf_line")"
                    fi
                fi
            fi

            if [[ "$platform" == "ds" ]]; then
                _diag_tick=$(( _diag_tick + 1 ))
                if (( _diag_tick % 3 == 0 )); then
                    print_ds_engine_status "$logfile"
                fi
            fi

            if (( _alive == 0 )); then
                printf "  [warn] %s stopped during measurement (PID=%s)\n" "$platform" "$pid"
                break
            fi

            sleep 1
        done
    ) &
    LIVE_MONITOR_PID=$!

    if [[ "$nowait" == "nowait" ]]; then
        # Fixed mode: caller will wait for process to finish
        return 0
    fi

    # Benchmark mode: measure for fixed duration, then stop
    printf "\n  Measuring for %d seconds (live FPS below)...\n\n" "${MEASURE_SECONDS}"
    local _elapsed=0
    while (( _elapsed < MEASURE_SECONDS )); do
        [[ "$ABORT" == true ]] && break
        sleep 1
        _elapsed=$(( _elapsed + 1 ))
    done

    # DeepStream first run may spend significant time compiling TensorRT engines.
    # If we stop too early, the round ends with no FPS and the next run repeats compilation.
    if [[ "$platform" == "ds" && "$ABORT" != true ]]; then
        if [[ "$(get_avg_fps "$logfile" "$platform")" == "0" ]] && is_ds_engine_build_log "$logfile"; then
            printf "\n  [info] DeepStream engine build detected. Extending wait up to %d seconds...\n" "${DS_ENGINE_BUILD_GRACE_SECONDS}"
            local _warm_elapsed=0
            while (( _warm_elapsed < DS_ENGINE_BUILD_GRACE_SECONDS )); do
                [[ "$ABORT" == true ]] && break
                if ! kill -0 "$pid" 2>/dev/null; then
                    break
                fi
                if is_oom_log "$logfile"; then
                    break
                fi
                if [[ "$(get_avg_fps "$logfile" "$platform")" != "0" ]]; then
                    printf "  [info] DeepStream produced FPS samples after engine build warm-up.\n"
                    break
                fi
                sleep 1
                _warm_elapsed=$(( _warm_elapsed + 1 ))
            done
        fi
    fi

    kill "${LIVE_MONITOR_PID}" 2>/dev/null
    wait "${LIVE_MONITOR_PID}" 2>/dev/null
    printf "\n"

    # collect FPS and stop docker
    _finish_round "$platform" "$pid" "$logfile" "$streams"
}

# Collect FPS from logfile, print diagnostics if needed, stop docker process.
# Sets global: ROUND_FPS, ROUND_STATUS
_finish_round() {
    local platform="$1"
    local pid="$2"
    local logfile="$3"
    local streams="${4:-0}"
    local container_name=""

    if [[ "$platform" == "dls" ]]; then
        container_name="${DLS_ACTIVE_CONTAINER}"
    else
        container_name="${DS_ACTIVE_CONTAINER}"
    fi

    ROUND_FPS=$(get_avg_fps "$logfile" "$platform" "$streams")
    if [[ "$platform" == "dls" ]]; then
        printf "  [%s] per-stream FPS (min cumulative average across streams): %s\n" "$platform" "${ROUND_FPS}"
    else
        printf "  [%s] avg per-source FPS (same value may repeat across all streams): %s\n" "$platform" "${ROUND_FPS}"
    fi

    if [[ "${ROUND_FPS}" == "0" ]]; then
        if is_oom_log "$logfile"; then
            ROUND_STATUS="oom"
        elif is_preroll_stall_log "$logfile"; then
            ROUND_STATUS="stalled-preroll"
        else
            ROUND_STATUS="no-fps"
        fi
        printf "  [warn] %s produced no FPS samples.\n" "$platform"
        if kill -0 "$pid" 2>/dev/null; then
            print_pipeline_diagnostics "${platform}" "$pid" "$logfile" "running-no-fps"
        else
            print_pipeline_diagnostics "${platform}" "$pid" "$logfile" "exited-no-fps"
        fi
    else
        ROUND_STATUS="ok"
    fi

    if kill -0 "$pid" 2>/dev/null; then
        printf "  [log] Stopping %s docker PID=%s\n" "$platform" "$pid"
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
        printf "  [log] %s docker PID=%s exit code: %s\n" "$platform" "$pid" "$?"
    else
        wait "$pid" 2>/dev/null
        printf "  [log] %s docker PID=%s already stopped, exit code: %s\n" "$platform" "$pid" "$?"
    fi

    if [[ "$platform" == "dls" ]]; then
        DLS_PID=""
    else
        DS_PID=""
    fi

    # Defensive cleanup in case docker client process was killed before container exited.
    ensure_container_absent "${container_name}"
}


# ==============================================================================
# Main benchmark: sequential — first DL Streamer, then DeepStream.

# run_phase <platform> <fixed_streams_or_empty> <fps_threshold> <result_var>
# If fixed_streams is set: runs exactly that many streams once (no threshold check).
# If empty: benchmark loop — increments until FPS drops below threshold.
# Sets result_var to max sustainable streams (benchmark) or fixed_streams (fixed mode).
run_phase() {
    local platform="$1"
    local fixed_streams="$2"
    local fps_threshold="$3"
    local -n _result_ref="$4"   # nameref to DLS_MAX_STREAMS or DS_MAX_STREAMS

    local label fps_scope streams _fps _status

    if [[ "$platform" == "dls" ]]; then
        label="DL Streamer"
        fps_scope="per-stream"
    else
        label="DeepStream"
        fps_scope="per-source"
    fi

    if [[ "$platform" == "ds" ]]; then
        printf "  [note] DeepStream reports per-source FPS; synchronized sources can show the same value on every stream.\n"
    fi

    if [[ -n "$fixed_streams" ]]; then
        # ---- Fixed mode: run exactly N streams, wait for natural completion ----
        printf "\n\n######################################################\n"
        printf " %s — fixed run: %d stream(s) (no timeout)\n" "$label" "$fixed_streams"
        printf "######################################################\n\n"

        BENCH_TMPDIR=$(mktemp -d)
        local logfile="${BENCH_TMPDIR}/${platform}.log"

        run_one_round "$platform" "$fixed_streams" "$logfile" "nowait" || true
        [[ "$ABORT" == true ]] && { kill "${LIVE_MONITOR_PID}" 2>/dev/null; rm -rf "${BENCH_TMPDIR}"; return; }

        local _pid="${ROUND_PID}"
        printf "\n  Pipeline running (PID=%s) — waiting for natural completion...\n" "$_pid"

        # Wait for the docker process to exit on its own
        while kill -0 "$_pid" 2>/dev/null; do
            [[ "$ABORT" == true ]] && break
            sleep 1
        done

        kill "${LIVE_MONITOR_PID}" 2>/dev/null
        wait "${LIVE_MONITOR_PID}" 2>/dev/null
        printf "\n"

        _finish_round "$platform" "$_pid" "$logfile" "$fixed_streams"
        _fps="${ROUND_FPS}"
        _status="${ROUND_STATUS}"

        printf "\n  [result] %s with %d stream(s) -> %-16s (avg %s FPS=%s)\n" \
            "$label" "$fixed_streams" "$_status" "$fps_scope" "$_fps"

        rm -rf "${BENCH_TMPDIR}"
        _result_ref="$fixed_streams"

    else
        # ---- Benchmark mode: find max sustainable streams ----
        printf "\n\n######################################################\n"
        printf " %s — finding max streams (threshold: %s FPS)\n" "$label" "$fps_threshold"
        printf "######################################################\n\n"

        streams=1
        while true; do
            [[ "$ABORT" == true ]] && break

            printf "======================================================\n"
            printf " [%s] Round: %d concurrent stream(s)\n" "$platform" "$streams"
            printf "======================================================\n"

            BENCH_TMPDIR=$(mktemp -d)
            local logfile="${BENCH_TMPDIR}/${platform}.log"

            run_one_round "$platform" "$streams" "$logfile" || true
            [[ "$ABORT" == true ]] && { rm -rf "${BENCH_TMPDIR}"; break; }
            _fps="${ROUND_FPS}"
            _status="${ROUND_STATUS}"

            printf "\n  [summary] %-20s -> %-16s (avg %s FPS=%s, threshold=%s)\n" \
                "$label" "$_status" "$fps_scope" "$_fps" "$fps_threshold"

            rm -rf "${BENCH_TMPDIR}"

            if awk -v fps="$_fps" -v thr="$fps_threshold" 'BEGIN { exit (fps + 0 < thr ? 0 : 1) }'; then
                local prev=$(( streams - 1 ))
                printf "\n  !! %s FPS (%s) dropped below threshold (%s FPS).\n" "$label" "$_fps" "$fps_threshold"
                if [[ $prev -gt 0 ]]; then
                    _result_ref=$prev
                    printf "  !! Maximum sustainable streams: %d\n" "$_result_ref"
                else
                    _result_ref=0
                    printf "  !! Cannot sustain even 1 stream above threshold FPS.\n"
                fi
                break
            else
                printf "  >> FPS OK. Increasing to %d stream(s)...\n\n" "$(( streams + 1 ))"
                streams=$(( streams + 1 ))
            fi
        done
    fi
}


DLS_MAX_STREAMS=0
DS_MAX_STREAMS=0

# Remove base-name containers leftover from crashed previous runs.
ensure_container_absent "${DLS_CONTAINER_NAME}"
ensure_container_absent "${DS_CONTAINER_NAME}"

# ---- Parallel fixed mode: both platforms simultaneously ----
if [[ -n "${DLS_FIXED_STREAMS}" && -n "${DS_FIXED_STREAMS}" && \
      "$RUN_DLS" == true && "$RUN_DS" == true && \
      ( -n "${INTEL_GPU}" || -e "/dev/accel" || -n "${INTEL_CPU}" ) && \
      -n "${NVIDIA_GPU}" ]]; then

    printf "\n\n######################################################\n"
    printf " Parallel fixed run: DLS=%s stream(s), DS=%s stream(s)\n" \
        "${DLS_FIXED_STREAMS}" "${DS_FIXED_STREAMS}"
    printf "######################################################\n\n"

    BENCH_TMPDIR=$(mktemp -d)
    DLS_LOG="${BENCH_TMPDIR}/dls.log"
    DS_LOG="${BENCH_TMPDIR}/ds.log"

    DLS_ROUND_INDEX=$(( DLS_ROUND_INDEX + 1 ))
    DS_ROUND_INDEX=$(( DS_ROUND_INDEX + 1 ))
    DLS_ACTIVE_CONTAINER="${DLS_CONTAINER_NAME}_${DLS_ROUND_INDEX}"
    DS_ACTIVE_CONTAINER="${DS_CONTAINER_NAME}_${DS_ROUND_INDEX}"
    ensure_container_absent "${DLS_ACTIVE_CONTAINER}"
    ensure_container_absent "${DS_ACTIVE_CONTAINER}"

    # Build and start DLS pipeline
    _dls_pipeline=$(build_dls_pipeline_no_encode "${DLS_FIXED_STREAMS}")
    printf "  [Intel / DL Streamer] Starting %d stream(s)\n" "${DLS_FIXED_STREAMS}"
    printf "    Pipeline: %s\n\n" "${_dls_pipeline}"
    _dls_cmd="${DLSTREAMER_DOCKER/--name ${DLS_CONTAINER_NAME}/--name ${DLS_ACTIVE_CONTAINER}} \"${_dls_pipeline}\""
    printf "    Docker command: %s\n\n" "${_dls_cmd}"
    eval "${_dls_cmd}" > "${DLS_LOG}" 2>&1 &
    DLS_PID=$!
    printf "    PID: %d\n" "${DLS_PID}"

    # Build and start DS pipeline
    _ds_pipeline=$(build_ds_pipeline_no_encode "${DS_FIXED_STREAMS}")
    printf "  [NVIDIA / DeepStream] Starting %d stream(s)\n" "${DS_FIXED_STREAMS}"
    printf "    Pipeline: %s\n\n" "${_ds_pipeline}"
    _ds_cmd="${DEEPSTREAM_DOCKER/--name ${DS_CONTAINER_NAME}/--name ${DS_ACTIVE_CONTAINER}} \"${_ds_pipeline}\""
    printf "    Docker command: %s\n\n" "${_ds_cmd}"
    eval "${_ds_cmd}" > "${DS_LOG}" 2>&1 &
    DS_PID=$!
    printf "    PID: %d\n\n" "${DS_PID}"

    # Wait for both to start
    _dls_sync=$(( 60 + DLS_FIXED_STREAMS * 15 ))
    _ds_sync=$(( 60 + DS_FIXED_STREAMS * 15 ))
    wait_for_pipeline_start "DL Streamer (${DLS_FIXED_STREAMS} streams)" "${DLS_PID}" "${DLS_LOG}" "${_dls_sync}"
    wait_for_pipeline_start "DeepStream (${DS_FIXED_STREAMS} streams)"   "${DS_PID}"  "${DS_LOG}"  "${_ds_sync}"

    # Combined live FPS monitor — DLS reads full logfile for combined per-stream view; DS reads new bytes.
    (
        _dls_off=0; _ds_off=0; _dls_dead=0; _ds_dead=0
        _diag_tick=0
        while true; do
            kill -0 "${DLS_PID}" 2>/dev/null || { [[ $_dls_dead -eq 0 ]] && printf "  [warn] DL Streamer stopped (PID=%s)\n" "${DLS_PID}"; _dls_dead=1; }
            kill -0 "${DS_PID}" 2>/dev/null  || { [[ $_ds_dead  -eq 0 ]] && printf "  [warn] DeepStream stopped (PID=%s)\n"  "${DS_PID}"; _ds_dead=1; }
            if [[ -f "${DLS_LOG}" ]]; then
                _size=$(stat -c %s "${DLS_LOG}" 2>/dev/null || echo 0)
                if (( _size > _dls_off )); then
                    _new=$(tail -c "+$((_dls_off + 1))" "${DLS_LOG}" 2>/dev/null)
                    _dls_off=$_size
                    _fps=$(grep 'FpsCounter(last' "${DLS_LOG}" 2>/dev/null \
                        | grep -oP 'per-stream=\K[0-9]+\.?[0-9]*' | tail -n "${DLS_FIXED_STREAMS}" | paste -sd ',' | sed 's/,/, /g')
                    if [[ -n "$_fps" ]]; then
                        printf "  [live] %-30s FPS per-stream: (%s)\n" "DL Streamer (${DLS_FIXED_STREAMS} streams):" "$_fps"
                    fi
                fi
            fi
            if [[ -f "${DS_LOG}" ]]; then
                _size=$(stat -c %s "${DS_LOG}" 2>/dev/null || echo 0)
                if (( _size > _ds_off )); then
                    _new=$(tail -c "+$((_ds_off + 1))" "${DS_LOG}" 2>/dev/null)
                    _ds_off=$_size
                    _perf_line=$(printf '%s\n' "$_new" | grep 'PERF' | tail -1)
                    [[ -n "$_perf_line" ]] && printf "  [live] %-30s %s\n" "DeepStream (${DS_FIXED_STREAMS} streams):" "$(normalize_ds_perf_line "$_perf_line")"
                fi
            fi

            _diag_tick=$(( _diag_tick + 1 ))
            if (( _diag_tick % 3 == 0 )) && kill -0 "${DS_PID}" 2>/dev/null; then
                print_ds_engine_status "${DS_LOG}"
            fi

            if (( _dls_dead == 1 && _ds_dead == 1 )); then
                break
            fi

            sleep 1
        done
    ) &
    LIVE_MONITOR_PID=$!

    printf "  Both pipelines running — waiting for natural completion...\n"
    while kill -0 "${DLS_PID}" 2>/dev/null || kill -0 "${DS_PID}" 2>/dev/null; do
        [[ "$ABORT" == true ]] && break
        sleep 1
    done

    kill "${LIVE_MONITOR_PID}" 2>/dev/null
    wait "${LIVE_MONITOR_PID}" 2>/dev/null
    printf "\n"

    _finish_round "dls" "${DLS_PID}" "${DLS_LOG}" "${DLS_FIXED_STREAMS}"
    DLS_MAX_STREAMS="${DLS_FIXED_STREAMS}"
    _dls_fps="${ROUND_FPS}"; _dls_status="${ROUND_STATUS}"

    _finish_round "ds" "${DS_PID}" "${DS_LOG}" "${DS_FIXED_STREAMS}"
    DS_MAX_STREAMS="${DS_FIXED_STREAMS}"
    _ds_fps="${ROUND_FPS}"; _ds_status="${ROUND_STATUS}"

    printf "\n  [result] DL Streamer  %s stream(s) -> %-16s (avg FPS=%s)\n" \
        "${DLS_FIXED_STREAMS}" "${_dls_status}" "${_dls_fps}"
    printf "  [result] DeepStream   %s stream(s) -> %-16s (avg FPS=%s)\n" \
        "${DS_FIXED_STREAMS}" "${_ds_status}" "${_ds_fps}"

    rm -rf "${BENCH_TMPDIR}"

else
    # ---- Sequential mode (benchmark or single-platform fixed) ----

    # ---- Phase 1: DL Streamer ----
    if [[ "$RUN_DLS" == true && ( -n "${INTEL_GPU}" || -e "/dev/accel" || -n "${INTEL_CPU}" ) ]]; then
        run_phase "dls" "${DLS_FIXED_STREAMS}" "${DLS_FPS_THRESHOLD}" DLS_MAX_STREAMS
    fi

    # ---- Phase 2: DeepStream ----
    if [[ "$RUN_DS" == true && -n "${NVIDIA_GPU}" ]]; then
        run_phase "ds" "${DS_FIXED_STREAMS}" "${DS_FPS_THRESHOLD}" DS_MAX_STREAMS
    fi

fi

printf "\n\n======================================================\n"
printf " BENCHMARK RESULTS\n"
printf "======================================================\n"
[[ "$RUN_DLS"  == true && ( -n "${INTEL_GPU}" || -e "/dev/accel" || -n "${INTEL_CPU}" ) ]] && \
    printf "  Intel  (DL Streamer) — max sustainable streams: %d  (threshold: %s FPS)\n" \
        "${DLS_MAX_STREAMS}" "${DLS_FPS_THRESHOLD}"
[[ "$RUN_DS" == true && -n "${NVIDIA_GPU}" ]] && \
    printf "  NVIDIA (DeepStream)  — max sustainable streams: %d  (threshold: %s FPS)\n" \
        "${DS_MAX_STREAMS}" "${DS_FPS_THRESHOLD}"
printf "======================================================\n"

if [[ "$RUN_DLS" == true && "$RUN_DS" == true && \
      ( -n "${INTEL_GPU}" || -e "/dev/accel" || -n "${INTEL_CPU}" ) && \
      -n "${NVIDIA_GPU}" && \
      "${DLS_MAX_STREAMS}" -gt 0 && "${DS_MAX_STREAMS}" -gt 0 ]]; then
    # Reconstruct original args without any existing --dls-streams/--ds-streams flags.
    _orig_args=()
    for _a in "$@"; do
        [[ "$_a" == --dls-streams=* || "$_a" == --ds-streams=* ]] && continue
        _orig_args+=("$_a")
    done
    printf "\n  To reproduce max-stream run for both platforms simultaneously:\n"
    printf "  %s %s --dls-streams=%d --ds-streams=%d\n" \
        "$0" "${_orig_args[*]}" "${DLS_MAX_STREAMS}" "${DS_MAX_STREAMS}"
fi
