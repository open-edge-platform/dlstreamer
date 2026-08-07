#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# ==============================================================================
# Pipeline builders (multi-source single gst-launch-1.0 process).
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

# Detect available Intel device nodes and prepare docker --device/--group-add args.
detect_intel_devices_for_docker() {
    # Check if there is /dev/dri folder to run on Intel GPU
    if [[ -e "/dev/dri" ]]; then
        DEVICE_DRI="--device /dev/dri --group-add $(stat -c "%g" /dev/dri/render* | head -1)"
    fi

    # Check if there is /dev/accel folder to run on Intel NPU
    if [[ -e "/dev/accel" ]]; then
        DEVICE_ACCEL="--device /dev/accel --group-add $(stat -c "%g" /dev/accel/accel* | head -1)"
    fi
}

# Determine DL Streamer input source element and extra bind mount from input arg.
determine_source_dls() {
    local input_dls="$1"
    local input_dir

    if [[ "$input_dls" =~ 'rtsp://' ]]; then
        SOURCE_INTEL="rtspsrc location=$input_dls"
        EXTRA_INPUT_VOLUME_INTEL=""
    elif [[ "$input_dls" =~ 'https://' ]]; then
        SOURCE_INTEL="urisourcebin buffer-size=4096 uri=$input_dls"
        EXTRA_INPUT_VOLUME_INTEL=""
    elif [[ "$input_dls" = /* ]]; then
        input_dir=$(dirname "$input_dls")
        SOURCE_INTEL="filesrc location=$input_dls"
        EXTRA_INPUT_VOLUME_INTEL="-v ${input_dir}:${input_dir}"
    else
        SOURCE_INTEL="filesrc location=/working_dir/$input_dls"
        EXTRA_INPUT_VOLUME_INTEL=""
    fi
}

# Determine DeepStream input source element and extra bind mount from input arg.
determine_source_ds() {
    local input_ds="$1"
    local input_dir

    if [[ "$input_ds" =~ 'rtsp://' ]]; then
        SOURCE_NVIDIA="rtspsrc location=$input_ds"
        EXTRA_INPUT_VOLUME_NVIDIA=""
    elif [[ "$input_ds" =~ 'https://' ]]; then
        SOURCE_NVIDIA="urisourcebin buffer-size=4096 uri=$input_ds"
        EXTRA_INPUT_VOLUME_NVIDIA=""
    elif [[ "$input_ds" = /* ]]; then
        input_dir=$(dirname "$input_ds")
        SOURCE_NVIDIA="filesrc location=$input_ds"
        EXTRA_INPUT_VOLUME_NVIDIA="-v ${input_dir}:${input_dir}"
    else
        SOURCE_NVIDIA="filesrc location=/working_dir/$input_ds"
        EXTRA_INPUT_VOLUME_NVIDIA=""
    fi
}

# Detect preferred Intel render device (prefer dGPU over iGPU) and update DRI args.
detect_preferred_intel_render_device() {
    INTEL_RENDER_DEVICE=""
    INTEL_OV_DEVICE="GPU"

    local _d _vendor _pci _dgpu_group
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

# Start one DL Streamer round in background.
# Dependencies expected in caller scope: DLS_ROUND_INDEX, DLS_ACTIVE_CONTAINER,
# DLS_CONTAINER_NAME, DLSTREAMER_DOCKER.
start_dls_round_container() {
    local streams="$1"
    local logfile="$2"
    local pipeline run_cmd

    DLS_ROUND_INDEX=$(( DLS_ROUND_INDEX + 1 ))
    DLS_ACTIVE_CONTAINER="${DLS_CONTAINER_NAME}_${DLS_ROUND_INDEX}"
    ensure_container_absent "${DLS_ACTIVE_CONTAINER}"
    pipeline=$(build_dls_pipeline_no_encode "$streams")
    printf "  [Intel / DL Streamer] Starting pipeline with %d stream branch(es)\n" "$streams"
    printf "    Pipeline: %s\n\n" "$pipeline"
    run_cmd="${DLSTREAMER_DOCKER/--name ${DLS_CONTAINER_NAME}/--name ${DLS_ACTIVE_CONTAINER}} \"${pipeline}\""
    printf "    Docker command: %s\n\n" "${run_cmd}"
    eval "${run_cmd}" > "$logfile" 2>&1 &
}

# Start one DeepStream round in background.
# Dependencies expected in caller scope: DS_ROUND_INDEX, DS_ACTIVE_CONTAINER,
# DS_CONTAINER_NAME, DEEPSTREAM_DOCKER.
start_ds_round_container() {
    local streams="$1"
    local logfile="$2"
    local pipeline run_cmd

    DS_ROUND_INDEX=$(( DS_ROUND_INDEX + 1 ))
    DS_ACTIVE_CONTAINER="${DS_CONTAINER_NAME}_${DS_ROUND_INDEX}"
    ensure_container_absent "${DS_ACTIVE_CONTAINER}"
    pipeline=$(build_ds_pipeline_no_encode "$streams")
    printf "  [NVIDIA / DeepStream] Starting pipeline with %d source(s)\n" "$streams"
    printf "    Pipeline: %s\n\n" "$pipeline"
    run_cmd="${DEEPSTREAM_DOCKER/--name ${DS_CONTAINER_NAME}/--name ${DS_ACTIVE_CONTAINER}} \"${pipeline}\""
    printf "    Docker command: %s\n\n" "${run_cmd}"
    eval "${run_cmd}" > "$logfile" 2>&1 &
}

# Store round process IDs in global benchmark state.
# Sets globals in caller scope: ROUND_PID, DLS_PID, DS_PID.
set_round_process_ids() {
    local platform="$1"
    local pid="$2"

    ROUND_PID=$pid
    if [[ "$platform" == "dls" ]]; then
        DLS_PID=$pid
    else
        DS_PID=$pid
    fi
}

# Wait for pipeline start, stop early on abort, and print diagnostics if it exits too soon.
wait_for_round_start_and_warn() {
    local platform="$1"
    local streams="$2"
    local pid="$3"
    local logfile="$4"
    local sync_timeout="$5"

    printf "    PID: %d\n" "$pid"
    wait_for_pipeline_start "${platform} (${streams} streams)" "$pid" "$logfile" "$sync_timeout"
    [[ "$ABORT" == true ]] && return 1

    # Warn if process already died before measurement window starts.
    if ! kill -0 "$pid" 2>/dev/null; then
        printf "  [warn] %s exited before measurement (PID=%s). Last log lines:\n" "$platform" "$pid"
        tail -n 10 "$logfile" 2>/dev/null | sed 's/^/    | /'
    fi
}

# Start background live FPS monitor for a running round.
# Sets global in caller scope: LIVE_MONITOR_PID.
start_live_fps_monitor() {
    local platform="$1"
    local streams="$2"
    local pid="$3"
    local logfile="$4"

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
}

# For DeepStream rounds, extend waiting when TensorRT engine build is detected
# and no FPS has been produced yet.
# Dependencies expected in caller scope: ABORT, DS_ENGINE_BUILD_GRACE_SECONDS.
run_ds_warmup_wait_if_needed() {
    local platform="$1"
    local pid="$2"
    local logfile="$3"

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
}

# Resolve active container name for a given platform.
active_container_name_for_platform() {
    local platform="$1"
    if [[ "$platform" == "dls" ]]; then
        printf "%s" "${DLS_ACTIVE_CONTAINER}"
    else
        printf "%s" "${DS_ACTIVE_CONTAINER}"
    fi
}

# Compute round FPS and status, and print diagnostics when no FPS is available.
# Sets globals in caller scope: ROUND_FPS, ROUND_STATUS.
evaluate_round_fps_and_status() {
    local platform="$1"
    local pid="$2"
    local logfile="$3"
    local streams="$4"

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
}

# Stop running docker client process for a round and clear platform PID state.
# Sets globals in caller scope: DLS_PID, DS_PID.
stop_round_process_and_clear_pid() {
    local platform="$1"
    local pid="$2"

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
}

# Evaluate round FPS against threshold, print decision logs, and update result.
# Usage: handle_phase_threshold_decision <label> <fps> <threshold> <streams> <result_var_name>
# Return: 0 when threshold is crossed (caller should break), 1 when next stream should run.
handle_phase_threshold_decision() {
    local label="$1"
    local fps="$2"
    local threshold="$3"
    local streams="$4"
    local result_var_name="$5"
    local -n _result_ref="$result_var_name"

    if awk -v fps="$fps" -v thr="$threshold" 'BEGIN { exit (fps + 0 < thr ? 0 : 1) }'; then
        local prev=$(( streams - 1 ))
        printf "\n  !! %s FPS (%s) dropped below threshold (%s FPS).\n" "$label" "$fps" "$threshold"
        if [[ $prev -gt 0 ]]; then
            _result_ref=$prev
            printf "  !! Maximum sustainable streams: %d\n" "$_result_ref"
        else
            _result_ref=0
            printf "  !! Cannot sustain even 1 stream above threshold FPS.\n"
        fi
        return 0
    fi

    printf "  >> FPS OK. Increasing to %d stream(s)...\n\n" "$(( streams + 1 ))"
    return 1
}

# Print benchmark configuration summary from caller-provided args and globals.
# Dependencies expected in caller scope: RUN_DLS, RUN_DS,
# DLS_FPS_THRESHOLD, DS_FPS_THRESHOLD, MEASURE_SECONDS.
print_configured_parameters() {
    local input_dls="$1"
    local input_ds="$2"
    local mode="$3"

    printf "Configuration:\n"
    printf "\t Input DLS     : %s\n" "$input_dls"
    printf "\t Input DS      : %s\n" "$input_ds"
    printf "\t Mode          : %s\n" "$mode"
    printf "\t Platforms     : %s\n" "$( [[ "$RUN_DLS" == true && "$RUN_DS" == true ]] && echo "Intel + NVIDIA" || ( [[ "$RUN_DLS" == true ]] && echo "Intel only" || echo "NVIDIA only" ) )"
    printf "\t DLS mode      : %s\n" "$(printf "benchmark (threshold: %s FPS)" "$DLS_FPS_THRESHOLD")"
    printf "\t DS  mode      : %s\n" "$(printf "benchmark (threshold: %s FPS)" "$DS_FPS_THRESHOLD")"
    printf "\t Measure time  : %s s\n" "${MEASURE_SECONDS}"
    printf "\n"
}

# Detect available Intel/NVIDIA hardware and print selected-platform banners.
# Sets globals in caller scope: INTEL_GPU, NVIDIA_GPU, INTEL_CPU.
# Usage: detect_available_hardware <argc>
detect_available_hardware() {
    local argc="$1"

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
        if [[ "$argc" -eq 0 ]]; then
            printf "Warning: No supported hardware detected. Skipping container-based model download.\n"
        else
            printf "Error: No supported hardware detected.\n"
            exit 1
        fi
    fi
}

# Build human-readable list of active platforms for summary messages.
# Sets globals in caller scope: ACTIVE_PLATFORMS, PLATFORM_SCOPE.
build_active_platform_scope() {
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
}

# Download DL Streamer LPR models when missing.
# Dependencies expected in caller scope: RUN_DLS, INTEL_GPU, INTEL_CPU,
# DLSTREAMER_DOCKER, DLS_CONTAINER_NAME.
dls_download_lpr_models() {
    if [[ "$RUN_DLS" == true && ( -n "${INTEL_GPU}" || -e "/dev/accel" || -n "${INTEL_CPU}" ) ]]; then
        if [[ ! -e "${PWD}/public/yolov8_license_plate_detector" ]]; then
            printf 'Downloading DL Streamer models....\n'
            eval "${DLSTREAMER_DOCKER/--name ${DLS_CONTAINER_NAME}/--name ${DLS_CONTAINER_NAME}_download} \"/opt/intel/dlstreamer/samples/download_public_models.sh yolov8_license_plate_detector,ch_PP-OCRv4_rec_infer\""
        else
            printf 'DL Streamer models already present, skipping download.\n'
        fi
    fi
}

# Download DeepStream TAO LPR models when missing.
# Dependencies expected in caller scope: RUN_DS, NVIDIA_GPU, DEEPSTREAM_DOCKER,
# DS_CONTAINER_NAME.
ds_download_lpr_models() {
    local DEEPSTREAM_SETUP_LPR
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
}

# When script is started without required args, print model availability and exit.
# Usage: check_model_availability_or_exit <argc>
# Dependencies expected in caller scope: print_usage.
check_model_availability_or_exit() {
    local argc="$1"

    if [[ "$argc" -eq 0 ]]; then
        local DLS_MODELS_OK=false
        local DS_MODELS_OK=false

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
        print_usage
        printf "Exiting script...\n\n"
        exit 1
    fi
}

# Wait until a newly-started pipeline reaches a running milestone or fails.
# Dependencies expected in caller scope: ABORT, is_oom_log, print_ds_engine_status.
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
            if [[ -f "$logfile" ]]; then
                local _last
                _last=$(grep -aE '.' "$logfile" 2>/dev/null | tail -n 1)
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

# Returns 0 (true) if given FPS is strictly below the threshold.
# Dependencies expected in caller scope: FPS_THRESHOLD.
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

# Normalize DeepStream PERF line for human-readable display.
normalize_ds_perf_line() {
    local line="$1"
    printf '%s\n' "$line" \
        | sed -E 's/\r//g; s/\)([0-9]+)FPS_/\)  FPS_/g; s/[[:space:]]+/ /g; s/^ //; s/ $//'
}

# Cleanup: kill docker processes started by benchmark script.
# Dependencies expected in caller scope: ABORT, LIVE_MONITOR_PID, DLS_PID, DS_PID, BENCH_TMPDIR.
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

# Print diagnostics for a failed/stalled pipeline log.
# Dependencies expected in caller scope: is_oom_log, is_preroll_stall_log.
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

# Report DeepStream engine file readiness for PGIE/SGIEs.
# Dependencies expected in caller scope: is_ds_engine_build_log, ds_engine_state.
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
    printf "Coexistence Benchmark:\n"
    printf "\tDetermines the maximum number of concurrent streams\n"
    printf "\tprocessed in a single gst-launch-1.0 process per platform.\n"
    printf "\n"
} # welcome


# ==============================================================================
# Just print how to use this script:
print_usage(){
    printf "Usage:\n"
    printf "\t coexistance_benchmark.sh <INPUT_DLS> <INPUT_DS> LPR [--dls-only|--ds-only]\n";
    printf "\n"
    printf "Arguments:\n"
    printf "\t INPUT_DLS     Input video file/stream for Intel platform (DL Streamer)\n"
    printf "\t INPUT_DS      Input video file/stream for NVIDIA platform (DeepStream)\n"
    printf "\t LPR           Pipeline mode (only LPR is supported)\n"
    printf "\n"
    printf "Options:\n"
    printf "\t --dls-only                 Run benchmark only on Intel GPU/NPU/CPU (DL Streamer)\n";
    printf "\t --ds-only                  Run benchmark only on NVIDIA GPU (DeepStream)\n";
    printf "\t --dls-fps-threshold=N      Minimum acceptable FPS for DL Streamer (default: 20)\n"
    printf "\t --ds-fps-threshold=N       Minimum acceptable FPS for DeepStream (default: 230)\n"
    printf "\t (default: fakesink output, run on both platforms, benchmark mode)\n"
    printf "\n"
    printf "Notes:\n"
    printf "\t Each round runs ONE docker per platform containing all N streams\n"
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
        print_usage
        return 1
    fi
}

# Handle startup argument flow: validate when args are provided,
# otherwise continue to model-availability check path.
handle_startup_arguments() {
    local argc="$1"
    local input_dls="$2"
    local input_ds="$3"
    local mode="$4"

    if [[ "$argc" -gt 0 ]]; then
        if ! validate_input_arguments "$input_dls" "$input_ds" "$mode"; then
            print_usage
            printf "Exiting script...\\n\\n"
            exit 1
        fi
    else
        printf "No input arguments provided. Checking model availability first...\n\n"
    fi
}
