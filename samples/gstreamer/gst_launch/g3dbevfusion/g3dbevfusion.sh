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
#   output  where the rendered frames go; omit to discard them (fakesink):
#             <dir>         numbered JPEG files
#             <file>.mp4    H.264 video, timed as the sequence was recorded
#             display       a window, played at the recorded pace
#
# FPS is the rate the sensors are read at (default 12, the rate of a sequence
# written by nuscenes_to_sequence.py; 2 for one written with --frames keyframes).
# Each frame's recorded capture time, from calibration.json, becomes its PTS.
# RENDER_OPTS replaces the g3drender properties, for example
#   RENDER_OPTS="view-mode=perspective forward-axis=y point-stride=2 width=2400 height=900"
#   RENDER_OPTS="view-mode=cam-proj cam-proj-index=0 point-stride=2 width=1600 height=900"
# ==============================================================================

set -euo pipefail

if [[ $# -lt 2 ]]; then
  sed -n '7,22p' "$0" | sed 's/^# \{0,1\}//'
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
# Every branch is read at the same nominal rate, so gvastreammux pairs frame N
# of each sensor; g3dbevfusion then restores the recorded timestamps.
FPS=${FPS:-12}
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
  # nuScenes lidar: +y is the vehicle's forward axis. A frame of ten sweeps has
  # ~270k points; draw every 8th.
  RENDER_OPTS=${RENDER_OPTS:-"forward-axis=y point-stride=8 width=2400 height=900"}
  if [[ "$OUTPUT" == "display" ]]; then
    SINK="g3drender ${RENDER_OPTS} ! videoconvert ! autovideosink sync=true"
  elif [[ "$OUTPUT" == *.mp4 ]]; then
    mkdir -p "$(dirname "$OUTPUT")"
    SINK="g3drender ${RENDER_OPTS} ! videoconvert ! video/x-raw,format=NV12 ! vah264enc rate-control=cqp ! "
    SINK+="h264parse ! mp4mux ! filesink location=${OUTPUT} sync=false"
  else
    mkdir -p "$OUTPUT"
    SINK="g3drender ${RENDER_OPTS} ! videoconvert ! jpegenc quality=90 ! multifilesink location=${OUTPUT}/%06d.jpg sync=false"
  fi
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
