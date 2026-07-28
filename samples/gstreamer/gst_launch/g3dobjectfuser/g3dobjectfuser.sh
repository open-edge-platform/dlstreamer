#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#
# Camera + LiDAR object fusion with g3dobjectfuser, reusing the g3dinference
# PointPillars demo dataset.
#
# Pipeline shape (detection runs per-camera BEFORE the mux; once a LiDAR stream
# joins gvastreammux its output is a CONTAINER batch that no video element can
# consume directly):
#
#   000002.png ! decode ! gvadetect           ! mux.sink_0
#   000002.bin ! g3dlidarparse ! g3dinference  ! mux.sink_1
#   gvastreammux ! g3dobjectfuser ! gvametaconvert ! gvametapublish (JSON)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DLSTREAMER_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"

# --- Inputs prepared by g3dinference_prepare.sh ------------------------------
PP_CACHE_DIR="${POINTPILLARS_CACHE_DIR:-${SCRIPT_DIR}/../g3dinference/.pointpillars}"
DATA_DIR="${PP_CACHE_DIR}/data"
PP_CONFIG="${PP_CONFIG:-${PP_CACHE_DIR}/config/pointpillars_ov_config.json}"

# The demo frame is index 2 (000002). Use multifilesrc %06d patterns with
# start-index == stop-index so each branch delivers exactly that one frame.
FRAME_INDEX="${FRAME_INDEX:-2}"
IMAGE="${DATA_DIR}/000002.png"
LIDAR="${DATA_DIR}/000002.bin"
CALIB_TXT="${DATA_DIR}/000002.txt"
IMAGE_PATTERN="${DATA_DIR}/%06d.png"
LIDAR_PATTERN="${DATA_DIR}/%06d.bin"

# --- YOLO model -------------------
MODELS_PATH="${MODELS_PATH:-${DLSTREAMER_ROOT}/../models}"
YOLO_NAME="${YOLO_NAME:-yolo11n}"
YOLO_MODEL="${YOLO_MODEL:-${MODELS_PATH}/public/${YOLO_NAME}/FP16/${YOLO_NAME}.xml}"

DEVICE="${DEVICE:-CPU}"
OUTPUT_JSON="${OUTPUT_JSON:-${SCRIPT_DIR}/g3dobjectfuser_output.json}"
CALIB_JSON="${CALIB_JSON:-${SCRIPT_DIR}/calib/kitti_000002.json}"
# LiDAR tracker coordinate frame: 'bev' (default, camera-independent) or 'image'.
TRACKING_SPACE="${TRACKING_SPACE:-bev}"

if [[ -z "${GST_DEBUG:-}" ]]; then
  export GST_DEBUG=g3dobjectfuser:4
fi

log() { printf '[g3dobjectfuser] %s\n' "$*"; }
fail() { printf '[g3dobjectfuser] ERROR: %s\n' "$*" >&2; exit 1; }

# --- Prerequisites -----------------------------------------------------------
for el in gvastreammux gvadetect g3dlidarparse g3dinference g3dobjectfuser gvametaconvert gvametapublish; do
  gst-inspect-1.0 "$el" >/dev/null 2>&1 || fail "Required GStreamer element not found: $el"
done

[[ -f "${LIDAR}" && -f "${IMAGE}" && -f "${CALIB_TXT}" ]] || fail \
  "Sample data missing under ${DATA_DIR} (000002.bin/png/txt). Run ../g3dinference/g3dinference_prepare.sh first."
[[ -f "${PP_CONFIG}" ]] || fail "PointPillars config not found: ${PP_CONFIG}. Run ../g3dinference/g3dinference_prepare.sh first."

# --- Download the YOLO model if needed ---------------------------------------
if [[ ! -f "${YOLO_MODEL}" ]]; then
  log "Downloading ${YOLO_NAME} into ${MODELS_PATH} via download_public_models.sh"
  MODELS_PATH="${MODELS_PATH}" "${DLSTREAMER_ROOT}/samples/download_public_models.sh" "${YOLO_NAME}"
fi
[[ -f "${YOLO_MODEL}" ]] || fail "YOLO model not found after download: ${YOLO_MODEL}"

# --- Build the calibration JSON from the KITTI 000002.txt --------------------
# g3dobjectfuser expects p2 (3x4), r0_rect and tr_velo_to_cam as 4x4. KITTI
# stores R0_rect as 3x3 and Tr_velo_to_cam as 3x4, so expand them here.
mkdir -p "$(dirname "${CALIB_JSON}")"
python3 - "${CALIB_TXT}" "${CALIB_JSON}" <<'PY'
import re, json, sys
txt = open(sys.argv[1]).read()
def row(name, n):
    m = re.search(rf"^{name}:\s*([-\d.eE+\s]+)", txt, re.M)
    if not m:
        raise SystemExit(f"calibration key not found: {name}")
    vals = [float(x) for x in m.group(1).split()[:n]]
    if len(vals) != n:
        raise SystemExit(f"{name}: expected {n} values, got {len(vals)}")
    return vals
P2 = row("P2", 12)
R0 = row("R0_rect", 9)
Tr = row("Tr_velo_to_cam", 12)
r0_4x4 = [R0[0],R0[1],R0[2],0, R0[3],R0[4],R0[5],0, R0[6],R0[7],R0[8],0, 0,0,0,1]
tr_4x4 = [Tr[0],Tr[1],Tr[2],Tr[3], Tr[4],Tr[5],Tr[6],Tr[7], Tr[8],Tr[9],Tr[10],Tr[11], 0,0,0,1]
json.dump({"p2": P2, "r0_rect": r0_4x4, "tr_velo_to_cam": tr_4x4},
          open(sys.argv[2], "w"), indent=2)
PY
log "Wrote calibration: ${CALIB_JSON}"

rm -f "${OUTPUT_JSON}"

# --- Run the pipeline --------------------------------------------------------
cmd=(
  gst-launch-1.0 -e
  multifilesrc "location=${IMAGE_PATTERN}" "start-index=${FRAME_INDEX}" "stop-index=${FRAME_INDEX}" "caps=image/png" "!"
  pngdec "!" videoconvert "!" video/x-raw,format=BGR "!"
  gvadetect "model=${YOLO_MODEL}" "device=${DEVICE}" "!" mux.sink_0
  multifilesrc "location=${LIDAR_PATTERN}" "start-index=${FRAME_INDEX}" "stop-index=${FRAME_INDEX}"
  "caps=application/octet-stream" "!"
  g3dlidarparse "!" g3dinference "config=${PP_CONFIG}" "device=${DEVICE}" "!" mux.sink_1
  gvastreammux name=mux output-mode=container sync-mode=first-pts "!"
  g3dobjectfuser "calibration=${CALIB_JSON}" "tracking-space=${TRACKING_SPACE}" "!"
  gvametaconvert format=json json-indent=2 "!"
  gvametapublish file-format=2 "file-path=${OUTPUT_JSON}" "!"
  fakesink
)

printf '%q ' "${cmd[@]}"
printf '\n'
"${cmd[@]}"

log "Done. Fused per-stream metadata written to: ${OUTPUT_JSON}"
