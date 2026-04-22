#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DLSTREAMER_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
POINTPILLARS_CACHE_DIR="${POINTPILLARS_CACHE_DIR:-${SCRIPT_DIR}/.pointpillars}"
DATA_DIR="${POINTPILLARS_CACHE_DIR}/data"
CONFIG_DIR="${POINTPILLARS_CACHE_DIR}/config"
DEFAULT_MODELS_PATH="${DLSTREAMER_ROOT}/../models"
MODELS_PATH="${MODELS_PATH:-${DEFAULT_MODELS_PATH}}"
POINTPILLARS_MODEL_DIR="${MODELS_PATH}/public/pointpillars/FP16"
OPENVINO_CONTRIB_URL="https://github.com/openvinotoolkit/openvino_contrib.git"
POINTPILLARS_MODEL_FILES=(
  "pointpillars_ov_nn.bin"
  "pointpillars_ov_nn.xml"
  "pointpillars_ov_pillar_layer.xml"
  "pointpillars_ov_postproc.xml"
)

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<'EOF'
Usage:
  ./g3dinference_prepare.sh

This script prepares assets required by g3dinference:
  - sample point cloud data
  - PointPillars IR files under MODELS_PATH/public/pointpillars/FP16
  - libov_pointpillars_extensions.so
  - pointpillars_ov_config.json with absolute paths

Environment:
  MODELS_PATH               Model root. Default: ../models next to the dlstreamer repo
  POINTPILLARS_ROOT         Existing modules/3d/pointPillars directory
  POINTPILLARS_CACHE_DIR    Cache directory for data/config/source checkout
  PYTHON                    Preferred Python interpreter with openvino installed
EOF
  exit 0
fi

log() {
  printf '[g3dinference-prepare] %s\n' "$*"
}

fail() {
  printf '[g3dinference-prepare] ERROR: %s\n' "$*" >&2
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "Required command not found: $1"
}

select_python_with_openvino() {
  local candidates=()

  if [[ -n "${PYTHON:-}" ]]; then
    candidates+=("${PYTHON}")
  fi

  candidates+=(
    "${DLSTREAMER_ROOT}/../.venv/bin/python"
    "${HOME}/.virtualenvs/dlstreamer/bin/python"
    "python3"
    "python"
  )

  local candidate
  for candidate in "${candidates[@]}"; do
    if command -v "$candidate" >/dev/null 2>&1; then
      if "$candidate" -c 'import openvino' >/dev/null 2>&1; then
        printf '%s\n' "$candidate"
        return 0
      fi
    elif [[ -x "$candidate" ]]; then
      if "$candidate" -c 'import openvino' >/dev/null 2>&1; then
        printf '%s\n' "$candidate"
        return 0
      fi
    fi
  done

  fail "Unable to find a Python interpreter with the openvino package installed"
}

resolve_pointpillars_root() {
  if [[ -n "${POINTPILLARS_ROOT:-}" ]]; then
    [[ -d "${POINTPILLARS_ROOT}" ]] || fail "POINTPILLARS_ROOT does not exist: ${POINTPILLARS_ROOT}"
    return 0
  fi

  local sibling_root="${DLSTREAMER_ROOT}/../openvino_contrib/modules/3d/pointPillars"
  if [[ -d "${sibling_root}" ]]; then
    POINTPILLARS_ROOT="${sibling_root}"
    return 0
  fi

  local sparse_root="${POINTPILLARS_CACHE_DIR}/openvino_contrib"
  if [[ ! -d "${sparse_root}/.git" ]]; then
    log "Cloning openvino_contrib PointPillars sources into ${sparse_root}"
    rm -rf "${sparse_root}"
    git clone --depth 1 --filter=blob:none --sparse "${OPENVINO_CONTRIB_URL}" "${sparse_root}"
    git -C "${sparse_root}" sparse-checkout set modules/3d/pointPillars
  fi

  POINTPILLARS_ROOT="${sparse_root}/modules/3d/pointPillars"
}

ensure_sample_data() {
  mkdir -p "${DATA_DIR}"

  local sample_path="${POINTPILLARS_ROOT}/pointpillars/dataset/demo_data/test/000002.bin"
  [[ -f "${sample_path}" ]] || fail "PointPillars sample point cloud not found: ${sample_path}"

  if [[ ! -f "${DATA_DIR}/000002.bin" ]]; then
    log "Copying sample point cloud from ${sample_path}"
    cp -f "${sample_path}" "${DATA_DIR}/000002.bin"
  fi
}

ensure_model_files() {
  mkdir -p "${POINTPILLARS_MODEL_DIR}"

  local file_name
  local pretrained_dir="${POINTPILLARS_ROOT}/pretrained"
  [[ -d "${pretrained_dir}" ]] || fail "PointPillars pretrained directory not found: ${pretrained_dir}"

  for file_name in "${POINTPILLARS_MODEL_FILES[@]}"; do
    local source_file="${pretrained_dir}/${file_name}"
    [[ -f "${source_file}" ]] || fail "PointPillars model file not found: ${source_file}"

    if [[ ! -f "${POINTPILLARS_MODEL_DIR}/${file_name}" ]]; then
      log "Copying ${file_name} from ${pretrained_dir}"
      cp -f "${source_file}" "${POINTPILLARS_MODEL_DIR}/${file_name}"
    fi
  done
}

build_extension() {
  local python_with_openvino
  python_with_openvino="$(select_python_with_openvino)"
  export PATH="$(dirname "${python_with_openvino}"):${PATH}"

  local ext_build_script="${POINTPILLARS_ROOT}/ov_extensions/build.sh"
  local ext_library="${POINTPILLARS_ROOT}/ov_extensions/build/libov_pointpillars_extensions.so"

  [[ -f "${ext_build_script}" ]] || fail "PointPillars extension build script not found: ${ext_build_script}"

  if [[ ! -f "${ext_library}" ]]; then
    log "Building PointPillars OpenVINO extension via ${ext_build_script}"
    bash "${ext_build_script}" 
  fi

  [[ -f "${ext_library}" ]] || fail "Expected extension library was not produced: ${ext_library}"
}

write_config() {
  mkdir -p "${CONFIG_DIR}"
  local pointpillars_config="${CONFIG_DIR}/pointpillars_ov_config.json"
  local pointpillars_extension_lib="${POINTPILLARS_ROOT}/ov_extensions/build/libov_pointpillars_extensions.so"

  cat > "${pointpillars_config}" <<EOF
{
  "voxel_params": {
    "voxel_size": [0.16, 0.16, 4],
    "point_cloud_range": [0, -39.68, -3, 69.12, 39.68, 1],
    "max_num_points": 32,
    "max_voxels": 16000
  },
  "extension_lib": "${pointpillars_extension_lib}",
  "voxel_model": "${POINTPILLARS_MODEL_DIR}/pointpillars_ov_pillar_layer.xml",
  "nn_model": "${POINTPILLARS_MODEL_DIR}/pointpillars_ov_nn.xml",
  "postproc_model": "${POINTPILLARS_MODEL_DIR}/pointpillars_ov_postproc.xml"
}
EOF

  log "Prepared PointPillars assets"
  log "  data: ${DATA_DIR}/000002.bin"
  log "  models: ${POINTPILLARS_MODEL_DIR}"
  log "  extension: ${pointpillars_extension_lib}"
  log "  config: ${pointpillars_config}"
}

require_command git
require_command cmake
require_command make
require_command c++

resolve_pointpillars_root
ensure_sample_data
ensure_model_files
build_extension
write_config