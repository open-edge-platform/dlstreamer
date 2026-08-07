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

# Imports:
. ./utils.sh

# Detect available Intel device nodes and prepare docker args.
detect_intel_devices_for_docker

# Determine GStreamer source elements from input arguments.
# INPUT_DLS ($1)
determine_source_dls "$1"

# INPUT_DS ($2)
determine_source_ds "$2"

# Detect preferred Intel render device (dGPU preferred over iGPU).
detect_preferred_intel_render_device

# Docker commands (must be defined AFTER setting EXTRA_INPUT_VOLUME_DLS/DS):

declare DLSTREAMER_DOCKER="docker run -i --rm --name benchmark_dls -v ${PWD}:/working_dir ${EXTRA_INPUT_VOLUME_DLS} ${DEVICE_DRI} ${DEVICE_ACCEL} \
-v ~/.Xauthority:/root/.Xauthority  -v /tmp/.X11-unix/:/tmp/.X11-unix/  -e DISPLAY=$DISPLAY  -v /dev/bus/usb:/dev/bus/usb \
--env MODELS_PATH=/working_dir \
intel/dlstreamer:latest /bin/bash -c"

declare DEEPSTREAM_DOCKER="docker run -i --rm --name benchmark_ds --network=host --gpus all -e DISPLAY=$DISPLAY --device /dev/snd \
-v /tmp/.X11-unix/:/tmp/.X11-unix -v ${PWD}:/working_dir ${EXTRA_INPUT_VOLUME_DS} -w /working_dir \
nvcr.io/nvidia/deepstream:8.0-samples-multiarch /bin/bash -c"

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
MEASURE_SECONDS=20
DLS_MAX_STREAMS=0
DS_MAX_STREAMS=0
OPTION_START_INDEX=4

for _arg in "${@:${OPTION_START_INDEX}}"; do
    case "$_arg" in
        --dls-only)               RUN_DS=false                          ;;
        --ds-only)                RUN_DLS=false                           ;;
        --dls-fps-threshold=*)    DLS_FPS_THRESHOLD="${_arg#*=}"            ;;
        --ds-fps-threshold=*)     DS_FPS_THRESHOLD="${_arg#*=}"             ;;
        --measure-seconds=*)      MEASURE_SECONDS="${_arg#*=}"              ;;
    esac
done

if [[ ! "$MEASURE_SECONDS" =~ ^[0-9]+$ || "$MEASURE_SECONDS" -le 0 ]]; then
    printf "Error: --measure-seconds must be a positive integer. Got: %s\n" "$MEASURE_SECONDS"
    print_usage
    exit 1
fi

# Benchmark configuration
DS_ENGINE_BUILD_GRACE_SECONDS=420
DLS_PID=""
DS_PID=""
LIVE_MONITOR_PID=""
BENCH_TMPDIR=""

trap 'cleanup; exit 130' SIGINT SIGTERM
trap cleanup EXIT

# Global abort flag — set by SIGINT/SIGTERM to break all loops cleanly.
ABORT=false


# Start: display welcome message and validate input arguments
welcome

handle_startup_arguments "$#" "$1" "$2" "$3"

# Print configured parameters:
if [[ "$#" -gt 0 ]]; then
    print_configured_parameters "$1" "$2" "$3"
fi

# Detect available hardware:
detect_available_hardware "$#"

# Build human-readable list of active platforms for summary messages
build_active_platform_scope

# Download DL Streamer models if needed:
dls_download_lpr_models

# Download DeepStream TAO models if needed:
ds_download_lpr_models

# If script started without required args, show model status and usage, then exit.
check_model_availability_or_exit "$#"


# Execute one measurement round for a single platform.
# Usage: run_one_round <platform> <streams> <logfile>
# platform: "dls" or "ds"
# Flow:
# 1) starts platform-specific docker pipeline in background,
# 2) waits for startup readiness and launches live FPS monitor,
# 3) measures for MEASURE_SECONDS (+ optional DS engine-build warm-up),
# 4) finalizes round (FPS/status evaluation, process stop, container cleanup).
# Sets global: ROUND_FPS, ROUND_STATUS, ROUND_PID
run_one_round() {
    local platform="$1"
    local streams="$2"
    local logfile="$3"
    local pid

    ROUND_FPS="0"
    ROUND_STATUS="no-fps"
    ROUND_PID=""

    local sync_timeout=$(( 60 + streams * 15 ))

    # Start platform-specific containerized pipeline in background.
    if [[ "$platform" == "dls" ]]; then
        start_dls_round_container "$streams" "$logfile"
    else
        start_ds_round_container "$streams" "$logfile"
    fi

    pid=$!

    # Save round PID in shared state for cleanup and diagnostics.
    set_round_process_ids "$platform" "$pid"

    # Wait until pipeline starts and warn if it exits too early.
    wait_for_round_start_and_warn "$platform" "$streams" "$pid" "$logfile" "$sync_timeout" || return 1

    # live FPS monitor (background) — DLS reads full logfile for combined per-stream view; DS reads new bytes.
    start_live_fps_monitor "$platform" "$streams" "$pid" "$logfile"

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
    run_ds_warmup_wait_if_needed "$platform" "$pid" "$logfile"

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

    # Resolve active container name for the current platform.
    container_name="$(active_container_name_for_platform "$platform")"

    # Compute round FPS/status and print diagnostics when needed.
    evaluate_round_fps_and_status "$platform" "$pid" "$logfile" "$streams"

    # Stop process and clear platform-specific PID state.
    stop_round_process_and_clear_pid "$platform" "$pid"

    # Defensive cleanup in case docker client process was killed before container exited.
    ensure_container_absent "${container_name}"
}


# ==============================================================================
# Main benchmark: sequential — first DL Streamer, then DeepStream.

# run_phase <platform> <fps_threshold> <result_var>
# Benchmark loop — increments until FPS drops below threshold.
# Sets result_var to max sustainable streams.
run_phase() {
    local platform="$1"
    local fps_threshold="$2"
    local result_var_name="$3"
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

    streams=1
    while true; do
        [[ "$ABORT" == true ]] && break

        printf "======================================================\n"
        printf " [%s] Round: %d concurrent stream(s)\n" "$platform" "$streams"
        printf "======================================================\n"

        BENCH_TMPDIR=$(mktemp -d)
        local logfile="${BENCH_TMPDIR}/${platform}.log"

        # Execute one benchmark round and collect ROUND_FPS/ROUND_STATUS globals.
        run_one_round "$platform" "$streams" "$logfile" || true
        [[ "$ABORT" == true ]] && { rm -rf "${BENCH_TMPDIR}"; break; }
        _fps="${ROUND_FPS}"
        _status="${ROUND_STATUS}"

        printf "\n  [summary] %-20s -> %-16s (avg %s FPS=%s, threshold=%s)\n" \
            "$label" "$_status" "$fps_scope" "$_fps" "$fps_threshold"

        rm -rf "${BENCH_TMPDIR}"

        # Compare FPS to threshold; update result and decide whether to stop phase.
        if handle_phase_threshold_decision "$label" "$_fps" "$fps_threshold" "$streams" "$result_var_name"; then
            break
        else
            streams=$(( streams + 1 ))
        fi
    done
}


# Remove stale base container name for DL Streamer.
ensure_container_absent "${DLS_CONTAINER_NAME}"
# Remove stale base container name for DeepStream.
ensure_container_absent "${DS_CONTAINER_NAME}"

# ---- Benchmark mode only: sequential (DL Streamer, then DeepStream) ----
# Run DL Streamer benchmark phase when Intel hardware path is available.
if [[ "$RUN_DLS" == true && ( -n "${INTEL_GPU}" || -e "/dev/accel" || -n "${INTEL_CPU}" ) ]]; then
    run_phase "dls" "${DLS_FPS_THRESHOLD}" DLS_MAX_STREAMS
fi

# Run DeepStream benchmark phase when NVIDIA GPU is available.
if [[ "$RUN_DS" == true && -n "${NVIDIA_GPU}" ]]; then
    run_phase "ds" "${DS_FPS_THRESHOLD}" DS_MAX_STREAMS
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


# ==============================================================================
# Optional: run both platforms in parallel at their found max stream counts.
if [[ "$ABORT" != true && ( "${DLS_MAX_STREAMS}" -gt 0 || "${DS_MAX_STREAMS}" -gt 0 ) ]]; then
    printf "\n"
    read -r -p "Run DL Streamer (${DLS_MAX_STREAMS}) and DeepStream (${DS_MAX_STREAMS}) containers in parallel now? [y/N] " _parallel_answer
    if [[ "${_parallel_answer}" =~ ^[Yy]$ ]]; then
        run_parallel_max_streams "${DLS_MAX_STREAMS}" "${DS_MAX_STREAMS}"
    else
        printf "Skipping parallel run.\n"
    fi
fi


