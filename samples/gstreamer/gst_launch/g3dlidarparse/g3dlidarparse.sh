#!/bin/bash
# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
set -e

LOCATION=${1:-velodyne/%06d.bin}
START_INDEX=${2:-250}
STRIDE=${3:-1}
FRAME_RATE=${4:-5}

if [ -z "$GST_DEBUG" ]; then
  export GST_DEBUG=g3dlidarparse:5
fi

cmd=(
  gst-launch-1.0
  multifilesrc
  "location=${LOCATION}"
  "start-index=${START_INDEX}"
  "caps=application/octet-stream"
  "!"
  g3dlidarparse
  "stride=${STRIDE}"
  "frame-rate=${FRAME_RATE}"
  "!"
  fakesink
)

printf '%q ' "${cmd[@]}"
printf '\n'
"${cmd[@]}"
