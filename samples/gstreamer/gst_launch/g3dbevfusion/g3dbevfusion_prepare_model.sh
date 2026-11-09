#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OPENVINO_CONTRIB_URL="${OPENVINO_CONTRIB_URL:-https://github.com/openvinotoolkit/openvino_contrib.git}"
# A fixed openvino_contrib master commit, so that the patches below always apply.
OPENVINO_CONTRIB_REF="${OPENVINO_CONTRIB_REF:-192fffa4a6533502d1a523e7853f5d982be57dd8}"
# openvino_bevfusion fixes that g3dbevfusion relies on (detection head weights,
# sparse encoder layout, OpenVINO 2026 compatibility, ...), from
# openvinotoolkit/openvino_contrib#1197. Drop them and move the commit above
# once that PR is merged.
PATCH_DIR="${SCRIPT_DIR}/patches"
CHECKPOINT_URL="https://download.openmmlab.com/mmdetection3d/v1.1.0_models/bevfusion/bevfusion_lidar-cam_voxel0075_second_secfpn_8xb4-cyclic-20e_nus-3d-5239b1af.pth"
TORCH_VERSION="${TORCH_VERSION:-2.13.0}"
TORCH_INDEX_URL="${TORCH_INDEX_URL:-https://download.pytorch.org/whl/cpu}"
EXTENSIONS=(bev_pool voxelize sparse_encoder)
EXTENSION_LIBS=(libopenvino_bevpool_extension.so libopenvino_voxelize_extension.so libopenvino_sparse_encoder_extension.so)
MODELS=(voxelize bev_pool sparse_encoder geometry camera_encoder_b6 full_detection)

usage() {
  cat <<'EOF'
Usage:
  ./g3dbevfusion_prepare_model.sh <openvino-version>

  openvino-version  Full OpenVINO version of the DL Streamer that will run the
                    pipeline, as published on PyPI, for example 2026.3.1

Prepares bevfusion_ov_model_<openvino-version>/, everything g3dbevfusion loads
at runtime, from openvino_contrib's openvino_bevfusion module:
  - g3dbevfusion.json, the element's config= file (paths relative to itself)
  - the OpenVINO IRs, exported from the published mmdetection3d BEVFusion checkpoint
  - the three custom-op extension libraries, built against that OpenVINO release
  - the OpenCL kernels of the GPU custom layers
It works in a temporary Python virtual environment with CPU PyTorch, NumPy and
the requested openvino package. The environment, sources, checkpoint and build
trees are removed at the end. The export needs an Intel GPU.

Required packages (Ubuntu):
  apt-get install git curl g++ python3-venv opencl-c-headers ocl-icd-opencl-dev

Environment:
  BEVFUSION_ROOT        Existing modules/openvino_bevfusion directory. Otherwise
                        openvino_contrib is fetched into a temporary directory.
  OPENVINO_CONTRIB_URL  Repository to fetch (default: openvinotoolkit/openvino_contrib)
  OPENVINO_CONTRIB_REF  Commit to check out (default: a fixed master commit). The
                        patches in patches/ are applied on top of it, and skipped
                        if the commit already contains them.
  PYTHON                Python to create the virtual environment with (default: python3)
  TORCH_VERSION         PyTorch version to install (default: 2.13.0)
  TORCH_INDEX_URL       Where to install PyTorch from (default: the CPU-only wheels)
  BEVFUSION_MODEL_DIR   Output directory (default: bevfusion_ov_model_<openvino-version>
                        next to this script)
EOF
}

log() {
  printf '[g3dbevfusion-prepare-model] %s\n' "$*"
}

fail() {
  printf '[g3dbevfusion-prepare-model] ERROR: %s\n' "$*" >&2
  exit 1
}

if [[ $# -ne 1 || "$1" == "-h" || "$1" == "--help" ]]; then
  usage
  [[ $# -eq 1 ]] && exit 0
  exit 1
fi
OV_VERSION="$1"
# All three parts: pip reads 2026.3 as 2026.3.0, and an extension built against
# 2026.3.0 does not load into 2026.3.1.
[[ "${OV_VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "Expected a full OpenVINO version such as 2026.3.1, got: ${OV_VERSION}"
OUT_DIR="${BEVFUSION_MODEL_DIR:-${SCRIPT_DIR}/bevfusion_ov_model_${OV_VERSION}}"

if [[ -f "${OUT_DIR}/g3dbevfusion.json" ]]; then
  log "${OUT_DIR} is already prepared; remove it to prepare it again"
  exit 0
fi
[[ ! -e "${OUT_DIR}" ]] || fail "${OUT_DIR} exists but is incomplete; remove it first"

PYTHON="${PYTHON:-python3}"
check_requirements() {
  local missing=()
  command -v git >/dev/null 2>&1 || missing+=(git)
  command -v curl >/dev/null 2>&1 || missing+=(curl)
  command -v c++ >/dev/null 2>&1 || missing+=(g++)
  "$PYTHON" -c 'import ensurepip, venv' 2>/dev/null || missing+=(python3-venv)
  # sparse_encoder calls OpenCL directly.
  if command -v c++ >/dev/null 2>&1; then
    printf '#include <CL/cl.h>\n' | c++ -fsyntax-only -x c++ - 2>/dev/null || missing+=(opencl-c-headers)
  fi
  [[ -e /usr/lib/x86_64-linux-gnu/libOpenCL.so || -e /usr/lib/libOpenCL.so ]] || missing+=(ocl-icd-opencl-dev)
  if [[ ${#missing[@]} -gt 0 ]]; then
    fail "Missing packages; install them with: apt-get install ${missing[*]}"
  fi
}
check_requirements

# Everything but the final output goes here. It sits next to the output so the
# finished directory can be moved into place in one step.
mkdir -p "$(dirname "${OUT_DIR}")"
WORK_DIR="$(mktemp -d "$(dirname "${OUT_DIR}")/.bevfusion_ov_model.XXXXXX")"
trap 'rm -rf "${WORK_DIR}"' EXIT

# An installed OpenVINO or DL Streamer environment (setupvars.sh, a DL Streamer
# image) puts its own openvino package and libraries on these paths. The
# virtual environment must use only the openvino it installs.
unset PYTHONPATH LD_LIBRARY_PATH

create_venv() {
  local venv="${WORK_DIR}/venv"
  log "Creating a Python virtual environment with openvino ${OV_VERSION} and torch ${TORCH_VERSION}"
  "$PYTHON" -m venv "${venv}"
  VENV_PYTHON="${venv}/bin/python"
  export PIP_NO_CACHE_DIR=1 PIP_DISABLE_PIP_VERSION_CHECK=1
  "${VENV_PYTHON}" -m pip install -q --index-url "${TORCH_INDEX_URL}" "torch==${TORCH_VERSION}" ||
    fail "Cannot install torch ${TORCH_VERSION} from ${TORCH_INDEX_URL}"
  "${VENV_PYTHON}" -m pip install -q "openvino==${OV_VERSION}" numpy cmake ninja ||
    fail "Cannot install openvino ${OV_VERSION}; is it a release published on PyPI?"
  PATH="${venv}/bin:${PATH}"
  # The openvino wheel carries the headers and CMake config the extensions
  # build against.
  OPENVINO_CMAKE_DIR="$("${VENV_PYTHON}" -c 'from openvino.utils import get_cmake_path; print(get_cmake_path(), end="")')"
  [[ -f "${OPENVINO_CMAKE_DIR}/OpenVINOConfig.cmake" ]] || fail "openvino ${OV_VERSION} has no CMake config to build the extensions against"
}

resolve_bevfusion_root() {
  if [[ -n "${BEVFUSION_ROOT:-}" ]]; then
    [[ -f "${BEVFUSION_ROOT}/export.py" ]] || fail "BEVFUSION_ROOT has no export.py: ${BEVFUSION_ROOT}"
    return
  fi
  local checkout="${WORK_DIR}/openvino_contrib"
  log "Fetching ${OPENVINO_CONTRIB_URL} (${OPENVINO_CONTRIB_REF})"
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
      fail "$(basename "${patch}") does not apply to openvino_contrib ${OPENVINO_CONTRIB_REF}"
    fi
  done
  BEVFUSION_ROOT="${checkout}/modules/openvino_bevfusion"
}

# Built where export.py loads them from, openvino_extensions/<ext>/build. An
# extension only loads into the OpenVINO release it was built against, so the
# same libraries serve the export and DL Streamer of that release. No RPATH:
# both load them into a process that already has libopenvino loaded.
build_extensions() {
  for ext in "${EXTENSIONS[@]}"; do
    local build="${BEVFUSION_ROOT}/openvino_extensions/${ext}/build"
    log "Building ${ext} extension"
    cmake -S "${BEVFUSION_ROOT}/openvino_extensions/${ext}" -B "${build}" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_SKIP_RPATH=ON \
      -DPython3_EXECUTABLE="${VENV_PYTHON}" -DOpenVINO_DIR="${OPENVINO_CMAKE_DIR}" >/dev/null
    cmake --build "${build}" >/dev/null
  done
}

prepare_checkpoint() {
  local raw="${WORK_DIR}/$(basename "${CHECKPOINT_URL}")"
  log "Downloading ${CHECKPOINT_URL}"
  curl -fL --retry 3 -o "${raw}" "${CHECKPOINT_URL}"
  log "Stripping the checkpoint to its weights"
  CHECKPOINT="${WORK_DIR}/bevfusion-det.pth"
  "${VENV_PYTHON}" "${BEVFUSION_ROOT}/tools/sanitize_checkpoint.py" "${raw}" "${CHECKPOINT}"
  rm -f "${raw}"
}

export_models() {
  EXPORT_DIR="${WORK_DIR}/export"
  log "Exporting OpenVINO IRs (takes several minutes)"
  (cd "${BEVFUSION_ROOT}" &&
    "${VENV_PYTHON}" export.py --checkpoint "${CHECKPOINT}" --output-dir "${EXPORT_DIR}" --no-quantize)
  for model in "${MODELS[@]}"; do
    [[ -f "${EXPORT_DIR}/${model}.xml" ]] || fail "Export did not produce ${model}.xml"
  done
}

# Copies only what g3dbevfusion loads into the output directory.
assemble() {
  local stage="${WORK_DIR}/bevfusion_ov_model" ext_src="${BEVFUSION_ROOT}/openvino_extensions"
  mkdir -p "${stage}/extensions" "${stage}/gpu_kernels"

  cp "${EXPORT_DIR}/config.json" "${stage}/"
  for model in "${MODELS[@]}"; do
    cp "${EXPORT_DIR}/${model}.xml" "${stage}/"
    # voxelize and bev_pool are a single custom op each and have no weights.
    [[ ! -f "${EXPORT_DIR}/${model}.bin" ]] || cp "${EXPORT_DIR}/${model}.bin" "${stage}/"
  done

  for i in "${!EXTENSIONS[@]}"; do
    cp "${ext_src}/${EXTENSIONS[$i]}/build/${EXTENSION_LIBS[$i]}" "${stage}/extensions/"
  done
  # The sparse encoder library reads its OpenCL kernel from its own directory.
  cp "${ext_src}/sparse_encoder/sparse_conv.cl" "${stage}/extensions/"

  # OpenVINO resolves the kernel sources relative to this file.
  cp "${ext_src}/gpu_custom_layers.xml" "${stage}/gpu_kernels/"
  local kernel
  for kernel in $(grep -o '<Source filename="[^"]*"' "${ext_src}/gpu_custom_layers.xml" | cut -d'"' -f2 | sort -u); do
    cp "${ext_src}/${kernel}" "${stage}/gpu_kernels/"
  done

  cat >"${stage}/g3dbevfusion.json" <<EOF
{
  "model_dir": ".",
  "extensions": [
    "extensions/${EXTENSION_LIBS[0]}",
    "extensions/${EXTENSION_LIBS[1]}",
    "extensions/${EXTENSION_LIBS[2]}"
  ],
  "gpu_custom_layers": "gpu_kernels/gpu_custom_layers.xml"
}
EOF
  mv "${stage}" "${OUT_DIR}"
}

create_venv
resolve_bevfusion_root
build_extensions
prepare_checkpoint
export_models
assemble

log "Prepared ${OUT_DIR} for OpenVINO ${OV_VERSION}"
log "  config: ${OUT_DIR}/g3dbevfusion.json"
