#!/bin/bash
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# shellcheck source=/dev/null
. /etc/os-release

PYTHON_BIN="${DLSTREAMER_PYTHON:-${VIRTUAL_ENV:+$VIRTUAL_ENV/bin/python}}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
    echo "Python interpreter not found: $PYTHON_BIN" >&2
    return 1
fi
# gvapython embeds Python, so expose the active interpreter's venv packages too.
PYTHON_VERSION_DIR="$("$PYTHON_BIN" -c 'import sys; print("python%d.%d" % sys.version_info[:2])')"
PYTHON_SITE_PACKAGES="$("$PYTHON_BIN" -c 'import sysconfig; print(sysconfig.get_path("purelib"))')"
GSTREAMER_PYTHON_PATH="/opt/intel/dlstreamer/gstreamer/lib/${PYTHON_VERSION_DIR}/site-packages:/opt/intel/dlstreamer/gstreamer/lib/${PYTHON_VERSION_DIR}/dist-packages:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages"

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
    export PYTHONPATH="${PYTHON_SITE_PACKAGES}:${GSTREAMER_PYTHON_PATH}:/opt/intel/dlstreamer/python${PYTHONPATH:+:$PYTHONPATH}"
else
    if [ "$ID" == "ubuntu" ] && [ "$VERSION_ID" == "22.04" ]; then
        export LD_LIBRARY_PATH=$LD_LIBRARY_PATH
    fi
    export LIBVA_DRIVERS_PATH=/usr/lib/x86_64-linux-gnu/dri
    export PYTHONPATH="${PYTHON_SITE_PACKAGES}:/opt/intel/openvino_genai/python:${GSTREAMER_PYTHON_PATH}:/opt/intel/dlstreamer/python${PYTHONPATH:+:$PYTHONPATH}"
fi
