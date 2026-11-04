#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CACHE_DIR="${BEVFUSION_CACHE_DIR:-${SCRIPT_DIR}/.bevfusion}"
OPENVINO_CONTRIB_URL="${OPENVINO_CONTRIB_URL:-https://github.com/openvinotoolkit/openvino_contrib.git}"
# A fixed openvino_contrib master commit, so that the patches below always apply.
OPENVINO_CONTRIB_REF="${OPENVINO_CONTRIB_REF:-192fffa4a6533502d1a523e7853f5d982be57dd8}"
# openvino_bevfusion fixes that g3dbevfusion relies on (detection head weights,
# sparse encoder layout, OpenVINO 2026 compatibility, ...), from
# openvinotoolkit/openvino_contrib#1197. Drop them and move the commit above
# once that PR is merged.
PATCH_DIR="${SCRIPT_DIR}/patches"
CHECKPOINT_URL="https://download.openmmlab.com/mmdetection3d/v1.1.0_models/bevfusion/bevfusion_lidar-cam_voxel0075_second_secfpn_8xb4-cyclic-20e_nus-3d-5239b1af.pth"
EXTENSIONS=(bev_pool voxelize sparse_encoder)

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<'EOF'
Usage:
  ./g3dbevfusion_prepare.sh

Prepares what g3dbevfusion needs, from openvino_contrib's openvino_bevfusion module:
  - the three custom-op extension libraries, built against DL Streamer's OpenVINO
  - the OpenVINO IRs, exported from the published mmdetection3d BEVFusion checkpoint
  - g3dbevfusion.json, the element's config= file
  - optionally, nuScenes scenes converted for multifilesrc (set NUSCENES_ROOT)

Environment:
  BEVFUSION_ROOT        Existing modules/openvino_bevfusion directory. Otherwise
                        openvino_contrib is cloned into the cache directory.
  OPENVINO_CONTRIB_URL  Repository to clone (default: openvinotoolkit/openvino_contrib)
  OPENVINO_CONTRIB_REF  Commit to check out (default: a fixed master commit). The
                        patches in patches/ are applied on top of it, and skipped
                        if the commit already contains them.
  PYTHON                Python with torch, numpy and openvino, for the IR export
                        (default: python3)
  OpenVINO_DIR          OpenVINO used by DL Streamer; set by OpenVINO's setupvars.sh
  BEVFUSION_CACHE_DIR   Output directory (default: .bevfusion next to this script)
  NUSCENES_ROOT         nuScenes root (holding samples/ and v1.0-mini/) to convert
  NUSCENES_VERSION      nuScenes version folder (default: v1.0-mini)
EOF
  exit 0
fi

log() {
  printf '[g3dbevfusion-prepare] %s\n' "$*"
}

fail() {
  printf '[g3dbevfusion-prepare] ERROR: %s\n' "$*" >&2
  exit 1
}

for cmd in git cmake make c++ curl; do
  command -v "$cmd" >/dev/null 2>&1 || fail "Required command not found: $cmd"
done
[[ -n "${OpenVINO_DIR:-}" ]] || fail "OpenVINO_DIR is not set; source DL Streamer's OpenVINO setupvars.sh first"

PYTHON="${PYTHON:-python3}"
# OpenVINO's setupvars.sh puts its own Python package and libraries on
# PYTHONPATH and LD_LIBRARY_PATH. A Python environment that has its own
# openvino package must not pick those up: mixed with torch's libraries the
# export crashes. Run such a Python with both variables cleared.
PYTHON_ENV=(env)
if env -u PYTHONPATH -u LD_LIBRARY_PATH "$PYTHON" -c 'import torch, numpy, openvino' 2>/dev/null; then
  PYTHON_ENV=(env -u PYTHONPATH -u LD_LIBRARY_PATH)
elif ! "$PYTHON" -c 'import torch, numpy, openvino' 2>/dev/null; then
  fail "$PYTHON cannot import torch, numpy and openvino, which the IR export needs; set PYTHON"
fi
run_python() {
  "${PYTHON_ENV[@]}" "$PYTHON" "$@"
}

resolve_bevfusion_root() {
  if [[ -n "${BEVFUSION_ROOT:-}" ]]; then
    [[ -f "${BEVFUSION_ROOT}/export.py" ]] || fail "BEVFUSION_ROOT has no export.py: ${BEVFUSION_ROOT}"
    return
  fi
  local checkout="${CACHE_DIR}/openvino_contrib"
  # Records which commit and patches the checkout was made from, so a cache
  # left by an older version of this script is replaced rather than reused.
  local stamp
  stamp="$(printf '%s %s\n' "${OPENVINO_CONTRIB_URL}" "${OPENVINO_CONTRIB_REF}"; cat "${PATCH_DIR}"/*.patch | git hash-object --stdin)"
  if [[ -d "${checkout}" && "$(cat "${checkout}/.g3dbevfusion-stamp" 2>/dev/null)" != "${stamp}" ]]; then
    log "Replacing ${checkout}, which was prepared from a different source"
    # The extensions built from the old sources have to be rebuilt too.
    rm -rf "${checkout}" "${CACHE_DIR}/extensions"
  fi
  if [[ ! -d "${checkout}" ]]; then
    log "Fetching ${OPENVINO_CONTRIB_URL} (${OPENVINO_CONTRIB_REF}) into ${checkout}"
    git init -q "${checkout}"
    git -C "${checkout}" remote add origin "${OPENVINO_CONTRIB_URL}"
    git -C "${checkout}" sparse-checkout set modules/openvino_bevfusion
    git -C "${checkout}" fetch -q --depth 1 --filter=blob:none origin "${OPENVINO_CONTRIB_REF}"
    git -C "${checkout}" checkout -q FETCH_HEAD
    for patch in "${PATCH_DIR}"/*.patch; do
      if git -C "${checkout}" apply --check "${patch}" 2>/dev/null; then
        log "Applying $(basename "${patch}")"
        git -C "${checkout}" apply "${patch}"
      elif git -C "${checkout}" apply --check --reverse "${patch}" 2>/dev/null; then
        log "Skipping $(basename "${patch}"), already in ${OPENVINO_CONTRIB_REF}"
      else
        rm -rf "${checkout}"
        fail "$(basename "${patch}") does not apply to openvino_contrib ${OPENVINO_CONTRIB_REF}"
      fi
    done
    printf '%s\n' "${stamp}" >"${checkout}/.g3dbevfusion-stamp"
  fi
  BEVFUSION_ROOT="${checkout}/modules/openvino_bevfusion"
}

# The extensions are built twice: export.py loads them from
# openvino_extensions/<ext>/build into the Python package's OpenVINO, while
# g3dbevfusion loads them into DL Streamer's, and an extension only loads into
# the OpenVINO version it was built against. $1 is the build directory with a
# %s for the extension name.
build_extensions() {
  local dest="$1" openvino_dir="$2"
  for ext in "${EXTENSIONS[@]}"; do
    local build
    build="$(printf "${dest}" "${ext}")"
    if [[ -z "$(find "${build}" -maxdepth 1 -name 'libopenvino_*_extension.so' 2>/dev/null)" ]]; then
      log "Building ${ext} extension in ${build}"
      cmake -S "${BEVFUSION_ROOT}/openvino_extensions/${ext}" -B "${build}" \
        -DCMAKE_BUILD_TYPE=Release -DOpenVINO_DIR="${openvino_dir}" >/dev/null
      cmake --build "${build}" -j"$(nproc)" >/dev/null
    fi
  done
}

prepare_checkpoint() {
  CHECKPOINT="${CACHE_DIR}/bevfusion-det.pth"
  [[ -f "${CHECKPOINT}" ]] && return
  local raw="${CACHE_DIR}/$(basename "${CHECKPOINT_URL}")"
  if [[ ! -f "${raw}" ]]; then
    log "Downloading ${CHECKPOINT_URL}"
    curl -fL --retry 3 -o "${raw}.part" "${CHECKPOINT_URL}"
    mv "${raw}.part" "${raw}"
  fi
  log "Stripping the checkpoint to its weights"
  run_python "${BEVFUSION_ROOT}/tools/sanitize_checkpoint.py" "${raw}" "${CHECKPOINT}"
}

export_models() {
  MODEL_DIR="${CACHE_DIR}/openvino_model"
  [[ -f "${MODEL_DIR}/full_detection.xml" && -f "${MODEL_DIR}/camera_encoder_b6.xml" ]] && return
  log "Exporting OpenVINO IRs to ${MODEL_DIR} (takes several minutes)"
  (cd "${BEVFUSION_ROOT}" &&
    run_python export.py --checkpoint "${CHECKPOINT}" --output-dir "${MODEL_DIR}" --no-quantize)
  for model in voxelize sparse_encoder geometry camera_encoder_b6 bev_pool full_detection; do
    [[ -f "${MODEL_DIR}/${model}.xml" ]] || fail "Export did not produce ${model}.xml"
  done
}

write_config() {
  local runtime_ext="${CACHE_DIR}/extensions"
  CONFIG="${CACHE_DIR}/g3dbevfusion.json"
  cat >"${CONFIG}" <<EOF
{
  "model_dir": "${MODEL_DIR}",
  "extensions": [
    "${runtime_ext}/bev_pool/libopenvino_bevpool_extension.so",
    "${runtime_ext}/voxelize/libopenvino_voxelize_extension.so",
    "${runtime_ext}/sparse_encoder/libopenvino_sparse_encoder_extension.so"
  ],
  "gpu_custom_layers": "${BEVFUSION_ROOT}/openvino_extensions/gpu_custom_layers.xml"
}
EOF
}

convert_nuscenes() {
  [[ -n "${NUSCENES_ROOT:-}" ]] || return 0
  log "Converting nuScenes scenes from ${NUSCENES_ROOT}"
  run_python "${SCRIPT_DIR}/nuscenes_to_sequence.py" --nuscenes-root "${NUSCENES_ROOT}" \
    --version "${NUSCENES_VERSION:-v1.0-mini}" --out "${CACHE_DIR}/nuscenes"
}

mkdir -p "${CACHE_DIR}"
resolve_bevfusion_root
PYTHON_OPENVINO_DIR="$(run_python -c 'from openvino.utils import get_cmake_path; print(get_cmake_path(), end="")')"
build_extensions "${BEVFUSION_ROOT}/openvino_extensions/%s/build" "${PYTHON_OPENVINO_DIR}"
build_extensions "${CACHE_DIR}/extensions/%s" "${OpenVINO_DIR}"
prepare_checkpoint
export_models
write_config
convert_nuscenes

log "Prepared g3dbevfusion assets"
log "  config:     ${CONFIG}"
log "  models:     ${MODEL_DIR}"
if [[ -n "${NUSCENES_ROOT:-}" ]]; then
  log "  sequences:  ${CACHE_DIR}/nuscenes/<scene>"
  log "Run: ./g3dbevfusion.sh ${CACHE_DIR}/nuscenes/<scene> ${CONFIG} fusion"
fi
