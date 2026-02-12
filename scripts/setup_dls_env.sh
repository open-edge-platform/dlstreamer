#!/bin/bash
# ==============================================================================
# Copyright (C) 2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# shellcheck source=/dev/null
. /etc/os-release

export LIBVA_DRIVER_NAME=iHD
export GST_PLUGIN_PATH=/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0:/opt/intel/dlstreamer/gstreamer/lib/
export LD_LIBRARY_PATH=/opt/intel/dlstreamer/gstreamer/lib:/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/lib/gstreamer-1.0:/usr/lib:/opt/intel/dlstreamer/lib:/usr/local/lib/gstreamer-1.0:/usr/local/lib:/opt/opencv:/opt/rdkafka:/opt/librealsense
export GST_VA_ALL_DRIVERS=1
export PATH=/opt/intel/dlstreamer/gstreamer/bin:/opt/intel/dlstreamer/bin:$PATH
export GST_PLUGIN_FEATURE_RANK=${GST_PLUGIN_FEATURE_RANK},ximagesink:MAX
export GI_TYPELIB_PATH=/opt/intel/dlstreamer/gstreamer/lib/girepository-1.0:/opt/intel/dlstreamer/lib/girepository-1.0:/usr/lib/x86_64-linux-gnu/girepository-1.0
if [ "$ID" == "fedora" ] || [ "$ID" == "rhel" ]; then
    export LIBVA_DRIVERS_PATH=/usr/lib64/dri-nonfree
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/opt/ffmpeg
else
    if [ "$ID" == "ubuntu" ] && [ "$VERSION_ID" == "22.04" ]; then
        export LD_LIBRARY_PATH=$LD_LIBRARY_PATH
    fi
    export LIBVA_DRIVERS_PATH=/usr/lib/x86_64-linux-gnu/dri
fi
