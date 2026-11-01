#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# BEVFusion 3D detection on a nuScenes scene converted by nuscenes_to_sequence.py.
#
# Usage: ./g3dbevfusion.sh <scene-dir> <runtime-config.json> [fusion|lidar] [frames] [output]
#   fusion  six cameras + lidar (default)
#   lidar   lidar only (no camera branches in the pipeline)
#   output  directory for rendered JPEG frames; omit to discard (fakesink)
# ==============================================================================

set -euo pipefail

if [[ $# -lt 2 ]]; then
  sed -n '7,13p' "$0" | sed 's/^# \{0,1\}//'
  exit 1
fi

SCENE=$(realpath "$1")
CONFIG=$(realpath "$2")
MODE=${3:-fusion}
FRAMES=${4:--1}
OUTPUT=${5:-}

STOP=-1
if [[ "$FRAMES" -gt 0 ]]; then
  STOP=$((FRAMES - 1))
fi
# Frames are paced by index (2 Hz nuScenes key frames); every branch must use
# the same rate so gvastreammux pairs frame N of each sensor.
FPS=2
CAMERAS=(CAM_FRONT CAM_FRONT_RIGHT CAM_FRONT_LEFT CAM_BACK CAM_BACK_LEFT CAM_BACK_RIGHT)

CAMERA_BRANCHES=""
CALIBRATION="${SCENE}/calibration.json"
if [[ "$MODE" == "fusion" ]]; then
  for i in "${!CAMERAS[@]}"; do
    CAMERA_BRANCHES+="multifilesrc location=${SCENE}/${CAMERAS[$i]}/%06d.jpg start-index=0 stop-index=${STOP} "
    CAMERA_BRANCHES+="caps=image/jpeg,framerate=${FPS}/1 ! jpegparse ! jpegdec idct-method=islow ! videoconvert ! "
    CAMERA_BRANCHES+="video/x-raw,format=BGR ! queue ! mux.sink_${i} "
  done
elif [[ "$MODE" == "lidar" ]]; then
  CALIBRATION="${SCENE}/calibration_lidar_only.json"
else
  echo "mode must be 'fusion' or 'lidar'" >&2
  exit 1
fi

if [[ -n "$OUTPUT" ]]; then
  mkdir -p "$OUTPUT"
  # nuScenes lidar: +y is the vehicle's forward axis, and ~35k points a frame (a
  # quarter of KITTI's), so draw every 2nd point instead of the default 16th.
  SINK="g3drender forward-axis=y point-stride=2 width=2400 height=900 ! videoconvert ! jpegenc quality=90 ! multifilesink location=${OUTPUT}/%06d.jpg"
else
  SINK="fakesink sync=false"
fi

# The lidar is always on pad 6, as declared in the calibration files.
# shellcheck disable=SC2086
gst-launch-1.0 -e \
  ${CAMERA_BRANCHES} \
  multifilesrc location="${SCENE}/LIDAR_TOP/%06d.bin" start-index=0 stop-index=${STOP} caps=application/octet-stream ! \
    g3dlidarparse frame-rate=${FPS} ! queue ! mux.sink_6 \
  gvastreammux name=mux output-mode=container sync-mode=first-pts max-wait-time=5000000000 max-queue-size=64 ! \
  g3dbevfusion config="${CONFIG}" calibration="${CALIBRATION}" ! \
  ${SINK}
