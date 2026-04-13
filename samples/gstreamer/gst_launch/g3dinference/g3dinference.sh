#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
POINTPILLARS_CACHE_DIR="${POINTPILLARS_CACHE_DIR:-${SCRIPT_DIR}/.pointpillars}"
DATA_DIR="${POINTPILLARS_CACHE_DIR}/data"
CONFIG_DIR="${POINTPILLARS_CACHE_DIR}/config"
START_INDEX_INPUT="${START_INDEX:-}"
STOP_INDEX_INPUT="${STOP_INDEX:-}"
STRIDE="${STRIDE:-1}"
FRAME_RATE="${FRAME_RATE:-5}"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
	cat <<'EOF'
Usage:
	./g3dinference.sh [SOURCE] [DEVICE] [OUTPUT_JSON] [SCORE_THRESHOLD]

Arguments:
	SOURCE           Optional. Either a multifilesrc pattern or a numerically named single frame file.
									 Default: .pointpillars/data/000002.bin
	DEVICE           Optional. OpenVINO device for g3dinference. Default: CPU
	OUTPUT_JSON      Optional. JSON output path. Default: .pointpillars/g3dinference_output.json
	SCORE_THRESHOLD  Optional. Minimum score threshold. Default: -1

Environment:
	POINTPILLARS_CACHE_DIR    Cache directory containing prepared data/config
	START_INDEX               Optional multifilesrc start index. Defaults to the source frame index for single-file input,
	                          otherwise 0.
	STOP_INDEX                Optional multifilesrc stop index. Defaults to the source frame index for single-file input.
	STRIDE                    Passed to g3dlidarparse. Default: 1
	FRAME_RATE                Passed to g3dlidarparse. Default: 5
	GST_DEBUG                 Defaults to g3dlidarparse:4,g3dinference:5 if unset.

Run ./g3dinference_prepare.sh first to prepare the sample data, models, extension,
and pointpillars_ov_config.json.
EOF
	exit 0
fi

SOURCE_INPUT="${1:-${DATA_DIR}/000002.bin}"
DEVICE="${2:-CPU}"
OUTPUT_JSON="${3:-${POINTPILLARS_CACHE_DIR}/g3dinference_output.json}"
SCORE_THRESHOLD="${4:--1}"
MULTIFILE_LOCATION=""
MULTIFILE_START_INDEX=""
MULTIFILE_STOP_INDEX=""

POINTPILLARS_CONFIG="${CONFIG_DIR}/pointpillars_ov_config.json"

if [[ -z "${GST_DEBUG:-}" ]]; then
	export GST_DEBUG="g3dlidarparse:4,g3dinference:5"
fi

log() {
	printf '[g3dinference] %s\n' "$*"
}

fail() {
	printf '[g3dinference] ERROR: %s\n' "$*" >&2
	exit 1
}

require_command() {
	command -v "$1" >/dev/null 2>&1 || fail "Required command not found: $1"
}

require_gst_element() {
	gst-inspect-1.0 "$1" >/dev/null 2>&1 || fail "Required GStreamer element not found: $1"
}

resolve_multifilesrc_input() {
	if [[ "${SOURCE_INPUT}" == *%* ]]; then
		MULTIFILE_LOCATION="${SOURCE_INPUT}"
		MULTIFILE_START_INDEX="${START_INDEX_INPUT:-0}"
		MULTIFILE_STOP_INDEX="${STOP_INDEX_INPUT:-}"
		return
	fi

	[[ -f "${SOURCE_INPUT}" ]] || fail "Input source file not found: ${SOURCE_INPUT}"

	local file_name stem extension width derived_index
	file_name="$(basename -- "${SOURCE_INPUT}")"
	stem="${file_name%.*}"
	extension="${file_name##*.}"

	[[ "${stem}" =~ ^[0-9]+$ ]] || fail "SOURCE must be a multifilesrc pattern or a numerically named frame file: ${SOURCE_INPUT}"

	width="${#stem}"
	derived_index="$((10#${stem}))"
	MULTIFILE_LOCATION="$(dirname -- "${SOURCE_INPUT}")/%0${width}d.${extension}"
	MULTIFILE_START_INDEX="${START_INDEX_INPUT:-${derived_index}}"
	MULTIFILE_STOP_INDEX="${STOP_INDEX_INPUT:-${derived_index}}"
}

validate_pipeline_prereqs() {
	require_command gst-launch-1.0
	require_command gst-inspect-1.0
	require_gst_element g3dlidarparse
	require_gst_element g3dinference
	require_gst_element gvametaconvert
	require_gst_element gvametapublish
}

build_pipeline_command() {
	PIPELINE_CMD=(gst-launch-1.0 -e)

	PIPELINE_CMD+=(
		multifilesrc
		"location=${MULTIFILE_LOCATION}"
		"start-index=${MULTIFILE_START_INDEX}"
	)

	if [[ -n "${MULTIFILE_STOP_INDEX}" ]]; then
		PIPELINE_CMD+=("stop-index=${MULTIFILE_STOP_INDEX}")
	fi

	PIPELINE_CMD+=(
		"caps=application/octet-stream"
		'!'
		g3dlidarparse
		"stride=${STRIDE}"
		"frame-rate=${FRAME_RATE}"
	)

	PIPELINE_CMD+=(
		'!'
		g3dinference
		"config=${POINTPILLARS_CONFIG}"
		"device=${DEVICE}"
		"score-threshold=${SCORE_THRESHOLD}"
		'!'
		gvametaconvert
		add-tensor-data=true
		format=json
		json-indent=2
		'!'
		gvametapublish
		file-format=2
		"file-path=${OUTPUT_JSON}"
		'!'
		fakesink
	)
}

if [[ ! -f "${POINTPILLARS_CONFIG}" ]]; then
	fail "PointPillars config is missing: ${POINTPILLARS_CONFIG}. Run ./g3dinference_prepare.sh first."
fi

validate_pipeline_prereqs
resolve_multifilesrc_input

mkdir -p "$(dirname "${OUTPUT_JSON}")"
rm -f "${OUTPUT_JSON}"

build_pipeline_command
printf '%q ' "${PIPELINE_CMD[@]}"
printf '\n'
"${PIPELINE_CMD[@]}"

log "Pipeline finished"
log "  output: ${OUTPUT_JSON}"
