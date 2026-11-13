#!/bin/bash
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
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
if [ "$ID" = "ubuntu" ] && [ "$VERSION_ID" = "22.04" ]; then
    if [ -n "${VIRTUAL_ENV:-}" ]; then
        DLS_PYTHON_BIN="$VIRTUAL_ENV/bin/python"
    elif [ -x "$HOME/python3venv/bin/python" ]; then
        DLS_PYTHON_BIN="$HOME/python3venv/bin/python"
        export PATH="$HOME/python3venv/bin:$PATH"
    fi

    if [ -n "${DLS_PYTHON_BIN:-}" ]; then
        if [ ! -x "$DLS_PYTHON_BIN" ]; then
            echo "Python interpreter not found: $DLS_PYTHON_BIN" >&2
            return 1
        fi
        DLS_PYTHON_VERSION="$("$DLS_PYTHON_BIN" -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
        if [ "$DLS_PYTHON_VERSION" != "3.11" ]; then
            echo "Ubuntu 22.04 requires Python 3.11; found $DLS_PYTHON_VERSION at $DLS_PYTHON_BIN" >&2
            return 1
        fi
        unset DLS_PYTHON_BIN DLS_PYTHON_VERSION
    fi
fi
export GST_PLUGIN_FEATURE_RANK=${GST_PLUGIN_FEATURE_RANK},ximagesink:MAX
export GI_TYPELIB_PATH=/opt/intel/dlstreamer/gstreamer/lib/girepository-1.0:/opt/intel/dlstreamer/lib/girepository-1.0:/usr/lib/x86_64-linux-gnu/girepository-1.0
if [ "$ID" == "fedora" ] || [ "$ID" == "rhel" ]; then
    export LIBVA_DRIVERS_PATH=/usr/lib64/dri-nonfree
    export PYTHONPATH=/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:/opt/intel/dlstreamer/python:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages
else
    export LIBVA_DRIVERS_PATH=/usr/lib/x86_64-linux-gnu/dri
    export PYTHONPATH=/opt/intel/openvino_genai/python:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:/opt/intel/dlstreamer/python:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages
fi
