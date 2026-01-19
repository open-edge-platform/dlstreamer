#!/bin/bash
# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
set -e

LOCATION=${1:-velodyne/%06d.bin}
START_INDEX=${2:-260}
STRIDE=${3:-5}
FRAME_RATE=${4:-5}

if [ -z "$GST_DEBUG" ]; then
  export GST_DEBUG=lidarparse:5
fi

PIPELINE="gst-launch-1.0 multifilesrc location=\"${LOCATION}\" start-index=${START_INDEX} caps=application/octet-stream ! \
  lidarparse stride=${STRIDE} frame-rate=${FRAME_RATE} ! fakesink"

echo "${PIPELINE}"
${PIPELINE}
