#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#
# Benchmark: each round launches ONE docker container per platform.
# The single gst-launch-1.0 inside that container grows by one stream branch
# each round, sharing model instances for efficiency.
# Benchmark uses fakesink output to measure pure inference/decode throughput.

. ./utils.sh

declare ERROR_CODE=0
declare DLS_CONTAINER_NAME="benchmark_dls"
declare DS_CONTAINER_NAME="benchmark_ds"
# Active (possibly unique) container names for the currently running round.
declare DLS_ACTIVE_CONTAINER="benchmark_dls"
declare DS_ACTIVE_CONTAINER="benchmark_ds"

# Parse optional platform flags (from $4 onwards: $3=MODE)
RUN_DLS=true
RUN_DS=true
DLS_FPS_THRESHOLD=30
DS_FPS_THRESHOLD=30
MEASURE_SECONDS=20
DLS_MAX_STREAMS=0
DS_MAX_STREAMS=0
DLS_START_STREAMS=1
DS_START_STREAMS=1
DLS_DECODE_CHAIN=1
OPTION_START_INDEX=4

for _arg in "${@:${OPTION_START_INDEX}}"; do
    case "$_arg" in
        --dls-only)               RUN_DS=false                          ;;
        --ds-only)                RUN_DLS=false                           ;;
        --dls-fps-threshold=*)    DLS_FPS_THRESHOLD="${_arg#*=}"            ;;
        --ds-fps-threshold=*)     DS_FPS_THRESHOLD="${_arg#*=}"             ;;
        --dls-start-streams=*)    DLS_START_STREAMS="${_arg#*=}"            ;;
        --ds-start-streams=*)     DS_START_STREAMS="${_arg#*=}"             ;;
        --dls-decode-chain=*)     DLS_DECODE_CHAIN="${_arg#*=}"             ;;
        --measure-seconds=*)      MEASURE_SECONDS="${_arg#*=}"              ;;
    esac
done

if [[ ! "$MEASURE_SECONDS" =~ ^[0-9]+$ || "$MEASURE_SECONDS" -le 0 ]]; then
    printf "Error: --measure-seconds must be a positive integer. Got: %s\n" "$MEASURE_SECONDS"
    print_usage
    exit 1
fi

if [[ ! "$DLS_START_STREAMS" =~ ^[0-9]+$ || "$DLS_START_STREAMS" -le 0 ]]; then
    printf "Error: --dls-start-streams must be a positive integer. Got: %s\n" "$DLS_START_STREAMS"
    print_usage
    exit 1
fi

if [[ ! "$DS_START_STREAMS" =~ ^[0-9]+$ || "$DS_START_STREAMS" -le 0 ]]; then
    printf "Error: --ds-start-streams must be a positive integer. Got: %s\n" "$DS_START_STREAMS"
    print_usage
    exit 1
fi

if [[ "$DLS_DECODE_CHAIN" != "1" && "$DLS_DECODE_CHAIN" != "2" ]]; then
    printf "Error: --dls-decode-chain must be 1 or 2. Got: %s\n" "$DLS_DECODE_CHAIN"
    print_usage
    exit 1
fi

DS_ENGINE_BUILD_GRACE_SECONDS=420
DLS_PID=""
DS_PID=""
LIVE_MONITOR_PID=""
BENCH_TMPDIR=""

trap 'cleanup; exit 130' SIGINT SIGTERM
trap cleanup EXIT

# Global abort flag — set by SIGINT/SIGTERM to break all loops cleanly.
ABORT=false


welcome

handle_startup_arguments "$#" "$1" "$2" "$3"

check_model_availability_or_exit "$#"

# Arguments are known valid from this point on — safe to touch hardware/docker setup.

detect_intel_devices_for_docker

determine_source_dls "$1"
determine_source_ds "$2"

print_detected_gpus

# Let the user choose the Intel GPU before render-device detection, so the
# choice actually affects DEVICE_DRI/DLSTREAMER_DOCKER built below.
select_intel_gpu

detect_preferred_intel_render_device

# Docker commands (must be defined AFTER setting EXTRA_INPUT_VOLUME_DLS/DS and DEVICE_DRI):

declare DLSTREAMER_DOCKER="docker run -i --rm --name benchmark_dls -v ${PWD}:/working_dir ${EXTRA_INPUT_VOLUME_DLS} ${DEVICE_DRI} ${DEVICE_ACCEL} \
-v ~/.Xauthority:/root/.Xauthority  -v /tmp/.X11-unix/:/tmp/.X11-unix/  -e DISPLAY=$DISPLAY  -v /dev/bus/usb:/dev/bus/usb \
--env MODELS_PATH=/working_dir \
intel/dlstreamer:latest /bin/bash -c"

declare DEEPSTREAM_DOCKER="docker run -i --rm --name benchmark_ds --network=host --gpus all -e DISPLAY=$DISPLAY --device /dev/snd \
-v /tmp/.X11-unix/:/tmp/.X11-unix -v ${PWD}:/working_dir ${EXTRA_INPUT_VOLUME_DS} -w /working_dir \
nvcr.io/nvidia/deepstream:8.0-samples-multiarch /bin/bash -c"

print_configured_parameters "$1" "$2" "$3"
detect_available_hardware "$#"
dls_download_lpr_models
ds_download_lpr_models


# Usage: run_one_round <platform> <streams> <logfile>
# Sets global: ROUND_FPS, ROUND_STATUS
run_one_round() {
    local platform="$1"
    local streams="$2"
    local logfile="$3"
    local pid

    ROUND_FPS="0"
    ROUND_STATUS="no-fps"

    local sync_timeout=$(( 60 + streams * 15 ))

    [[ "$platform" == "dls" ]] \
        && start_dls_round_container "$streams" "$logfile" \
        || start_ds_round_container "$streams" "$logfile"

    pid=$!
    set_round_process_ids "$platform" "$pid"

    if ! wait_for_round_start_and_warn "$platform" "$streams" "$pid" "$logfile" "$sync_timeout"; then
        _finish_round "$platform" "$pid" "$logfile" "$streams"
        return 1
    fi

    start_live_fps_monitor "$platform" "$streams" "$pid" "$logfile"

    printf "\n  Measuring for %d seconds (live FPS below)...\n\n" "${MEASURE_SECONDS}"
    local _elapsed=0
    while (( _elapsed < MEASURE_SECONDS )); do
        [[ "$ABORT" == true ]] && break
        sleep 1
        _elapsed=$(( _elapsed + 1 ))
    done

    # Avoids ending the round with no FPS while DeepStream is still compiling TensorRT engines.
    run_ds_warmup_wait_if_needed "$platform" "$pid" "$logfile"

    kill "${LIVE_MONITOR_PID}" 2>/dev/null
    wait "${LIVE_MONITOR_PID}" 2>/dev/null
    printf "\n"

    _finish_round "$platform" "$pid" "$logfile" "$streams"
}

# Sets global: ROUND_FPS, ROUND_STATUS
_finish_round() {
    local platform="$1"
    local pid="$2"
    local logfile="$3"
    local streams="${4:-0}"
    local container_name=""

    container_name="$(active_container_name_for_platform "$platform")"
    evaluate_round_fps_and_status "$platform" "$pid" "$logfile" "$streams"
    stop_round_process_and_clear_pid "$platform" "$pid"

    # Defensive: docker client process may have been killed before its container exited.
    ensure_container_absent "${container_name}"
}


# Usage: run_phase <platform> <fps_threshold> <result_var> <start_streams>
# Increments streams until FPS drops below threshold; sets result_var to max sustainable streams.
run_phase() {
    local platform="$1"
    local fps_threshold="$2"
    local result_var_name="$3"
    local start_streams="${4:-1}"
    local -n _result_ref="$result_var_name"   # nameref to DLS_MAX_STREAMS or DS_MAX_STREAMS

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

    printf "\n\n######################################################\n"
    printf " %s — finding max streams (threshold: %s FPS)\n" "$label" "$fps_threshold"
    printf "######################################################\n\n"

    streams="$start_streams"
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
        print_fps_statistics_table "$logfile" "$platform"

        rm -rf "${BENCH_TMPDIR}"

        handle_phase_threshold_decision "$label" "$_fps" "$fps_threshold" "$streams" "$result_var_name" \
            && break \
            || streams=$(( streams + 1 ))
    done
}


ensure_container_absent "${DLS_CONTAINER_NAME}"
ensure_container_absent "${DS_CONTAINER_NAME}"

if [[ "$RUN_DLS" == true && ( -n "${INTEL_GPU}" || -e "/dev/accel" || -n "${INTEL_CPU}" ) ]]; then
    run_phase "dls" "${DLS_FPS_THRESHOLD}" DLS_MAX_STREAMS "${DLS_START_STREAMS}"
fi

if [[ "$RUN_DS" == true && -n "${NVIDIA_GPU}" ]]; then
    run_phase "ds" "${DS_FPS_THRESHOLD}" DS_MAX_STREAMS "${DS_START_STREAMS}"
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


# Optional: run both platforms in parallel at their found max stream counts.
if [[ "$ABORT" != true && ( "${DLS_MAX_STREAMS}" -gt 0 || "${DS_MAX_STREAMS}" -gt 0 ) ]]; then
    printf "\n"
    read -r -p "Run DL Streamer (${DLS_MAX_STREAMS}) and DeepStream (${DS_MAX_STREAMS}) containers in parallel now? [y/N] " _parallel_answer
    [[ "${_parallel_answer}" =~ ^[Yy]$ ]] \
        && run_parallel_max_streams "${DLS_MAX_STREAMS}" "${DS_MAX_STREAMS}" \
        || printf "Skipping parallel run.\n"
fi


