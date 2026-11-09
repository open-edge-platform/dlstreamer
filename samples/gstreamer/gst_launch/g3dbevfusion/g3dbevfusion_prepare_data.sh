#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${NUSCENES_SEQ_DIR:-${SCRIPT_DIR}/nuscenes_mini_seq}"
NUSCENES_MINI_URL="${NUSCENES_MINI_URL:-https://www.nuscenes.org/data/v1.0-mini.tgz}"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<'EOF'
Usage:
  ./g3dbevfusion_prepare_data.sh

Prepares nuscenes_mini_seq/<scene>/, the nuScenes v1.0-mini scenes converted
for multifilesrc by nuscenes_to_sequence.py: numbered camera images and LiDAR
point clouds per sensor, the calibration, and the ground-truth boxes. The
downloaded archive and the extracted dataset are temporary and removed at the end.

Environment:
  NUSCENES_ROOT       Existing nuScenes root (holding samples/ and v1.0-mini/) to
                      convert instead of downloading v1.0-mini; left untouched.
  NUSCENES_VERSION    nuScenes version folder under NUSCENES_ROOT (default: v1.0-mini)
  NUSCENES_MINI_URL   Where to download v1.0-mini.tgz from
  PYTHON              Python with numpy (default: python3)
  NUSCENES_SEQ_DIR    Output directory (default: nuscenes_mini_seq next to this script)
EOF
  exit 0
fi

log() {
  printf '[g3dbevfusion-prepare-data] %s\n' "$*"
}

fail() {
  printf '[g3dbevfusion-prepare-data] ERROR: %s\n' "$*" >&2
  exit 1
}

if [[ -d "${OUT_DIR}" && -n "$(ls -A "${OUT_DIR}")" ]]; then
  log "${OUT_DIR} is already prepared; remove it to prepare it again"
  exit 0
fi

PYTHON="${PYTHON:-python3}"
"$PYTHON" -c 'import numpy' 2>/dev/null || fail "$PYTHON cannot import numpy; set PYTHON"

# Everything but the final output goes here. It sits next to the output so the
# finished directory can be moved into place in one step.
mkdir -p "$(dirname "${OUT_DIR}")"
WORK_DIR="$(mktemp -d "$(dirname "${OUT_DIR}")/.nuscenes_mini_seq.XXXXXX")"
trap 'rm -rf "${WORK_DIR}"' EXIT

if [[ -z "${NUSCENES_ROOT:-}" ]]; then
  command -v curl >/dev/null 2>&1 || fail "Required command not found: curl"
  log "Downloading ${NUSCENES_MINI_URL} (about 4 GB)"
  curl -fL --retry 3 -o "${WORK_DIR}/v1.0-mini.tgz" "${NUSCENES_MINI_URL}"
  # Only the key frames and the metadata tables are converted; sweeps/ and
  # maps/ are not needed.
  log "Extracting"
  NUSCENES_ROOT="${WORK_DIR}/nuscenes"
  mkdir -p "${NUSCENES_ROOT}"
  tar -xzf "${WORK_DIR}/v1.0-mini.tgz" -C "${NUSCENES_ROOT}" samples v1.0-mini
  rm -f "${WORK_DIR}/v1.0-mini.tgz"
fi

log "Converting nuScenes scenes from ${NUSCENES_ROOT}"
"$PYTHON" "${SCRIPT_DIR}/nuscenes_to_sequence.py" --nuscenes-root "${NUSCENES_ROOT}" \
  --version "${NUSCENES_VERSION:-v1.0-mini}" --out "${WORK_DIR}/nuscenes_mini_seq"
rmdir "${OUT_DIR}" 2>/dev/null || true
mv "${WORK_DIR}/nuscenes_mini_seq" "${OUT_DIR}"

log "Prepared ${OUT_DIR}"
log "  scenes: $(ls "${OUT_DIR}" | tr '\n' ' ')"
