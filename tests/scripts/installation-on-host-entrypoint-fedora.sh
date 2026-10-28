#!/bin/bash
# ==============================================================================
# Copyright (C) 2024-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
set -e
dls_package=$1
# Optional second argument overrides the version used in docker/fedora41/fedora41.Dockerfile.
OPENVINO_GENAI_VERSION_OVERRIDE=${2:-}
DEFAULT_OPENVINO_GENAI_VERSION=2026.5.0.0.dev20261027

# Function to display text in a given color
echo_color() {
    local text="$1"
    local color="$2"
    local color_code=""

    # Determine the color code based on the color name
    case "$color" in
        black) color_code="\e[30m" ;;
        red) color_code="\e[31m" ;;
        green) color_code="\e[32m" ;;
        bred) color_code="\e[91m" ;;
        bgreen) color_code="\e[92m" ;;
        yellow) color_code="\e[33m" ;;
        blue) color_code="\e[34m" ;;
        magenta) color_code="\e[35m" ;;
        cyan) color_code="\e[36m" ;;
        white) color_code="\e[37m" ;;
        *) echo "Invalid color name"; return 1 ;;
    esac

    # Display the text in the chosen color
    echo -e "${color_code}${text}\e[0m"
}

# Function to handle errors
handle_error() {
    echo -e "\e[31m Error occurred: $1\e[0m"
    exit 1
}

# Install the same OpenVINO GenAI archive used by the Fedora Docker image.
# It bundles the OpenVINO runtime as well as the Python bindings.
install_openvino() {
    local openvino_genai_version="${OPENVINO_GENAI_VERSION_OVERRIDE:-$DEFAULT_OPENVINO_GENAI_VERSION}"
    local openvino_genai_ver="openvino_genai_rhel9_${openvino_genai_version}_x86_64"
    local openvino_genai_pkg="https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/nightly/${openvino_genai_version}/${openvino_genai_ver}.tar.gz"
    local genai_version_marker=/opt/intel/openvino_genai/.installed_version

    echo_color "Installing OpenVINO GenAI ${openvino_genai_version} alongside intel-dlstreamer..." "blue"
    sudo mkdir -p /opt/intel

    if [ -d /opt/intel/openvino_genai ] && [ "$(cat "$genai_version_marker" 2>/dev/null)" != "$openvino_genai_version" ]; then
        sudo rm -rf /opt/intel/openvino_genai
    fi
    if [ ! -d /opt/intel/openvino_genai ]; then
        local tmp_genai_dir
        tmp_genai_dir=$(mktemp -d)
        curl -fL "$openvino_genai_pkg" | tar -xz -C "$tmp_genai_dir"
        sudo mv "${tmp_genai_dir}/${openvino_genai_ver}" /opt/intel/openvino_genai
        rm -rf "$tmp_genai_dir"
        echo "$openvino_genai_version" | sudo tee "$genai_version_marker" > /dev/null
    fi

    sudo ln -sfn /opt/intel/openvino_genai /opt/intel/openvino
    echo "/opt/intel/openvino/runtime/lib/intel64" | sudo tee /etc/ld.so.conf.d/openvino.conf > /dev/null
    sudo ldconfig
    echo_color "OpenVINO installed and registered with ldconfig" "green"
}

# Stop and remove all running Docker containers
echo_color "Stopping all running Docker containers" "blue"
docker ps -q | xargs -r docker stop || true
echo_color "Removing all stopped Docker containers" "blue"
docker ps -a -q | xargs -r docker rm || true
sudo docker system prune -a -f

# List remaining Docker containers
echo_color "Listing all remaining Docker containers" "blue"
docker ps -a

#Remove gstreamer .cache
if [ -f /home/labrat/.cache/gstreamer-1.0/ ]; then
    sudo rm -rf /home/labrat/.cache/gstreamer-1.0/
    echo_color "Removed /home/labrat/.cache/gstreamer-1.0/" "blue"
fi

#DLStreamer installation

mkdir /home/labrat/intel/dls_package
cd /home/labrat/intel/dls_package

echo_color "Downloading DLS rpm package version: $dls_package" "blue"
wget -r -np -nH --cut-dirs=7 -A '*intel-dlstreamer*' "$dls_package"
echo_color "Downloaded DLS package version: $dls_package" "magenta"

install_openvino

echo_color "Executing: sudo dnf install -y ./intel-dlstreamer*" "blue"
sudo dnf install -y ./intel-dlstreamer*
echo_color "Completed: sudo dnf install -y ./intel-dlstreamer*" "magenta"

#unset env variables
echo_color "Unsetting environment variables" "blue"
unset LIBVA_DRIVER_NAME
unset GST_PLUGIN_PATH
unset LD_LIBRARY_PATH
unset LIBVA_DRIVERS_PATH
unset GST_VA_ALL_DRIVERS
unset MODEL_PROC_PATH
unset PYTHONPATH
unset TERM

# Display the values of the environment variables
echo_color "Displaying the values of the environment variables:" "blue"
echo "LIBVA_DRIVER_NAME: ${LIBVA_DRIVER_NAME}"
echo "GST_PLUGIN_PATH: ${GST_PLUGIN_PATH}"
echo "LD_LIBRARY_PATH: ${LD_LIBRARY_PATH}"
echo "LIBVA_DRIVERS_PATH: ${LIBVA_DRIVERS_PATH}"
echo "GST_VA_ALL_DRIVERS: ${GST_VA_ALL_DRIVERS}"
echo "MODEL_PROC_PATH: ${MODEL_PROC_PATH}"
echo "PYTHONPATH: ${PYTHONPATH}"
echo "TERM: ${TERM}"


# set environment variables
echo_color "Setting the environment variables" "blue"
# shellcheck source=/dev/null
source /opt/intel/dlstreamer/scripts/setup_dls_env.sh
export MODEL_PROC_PATH=/opt/intel/dlstreamer/samples/gstreamer/model_proc
export PATH=/python3venv/bin:$PATH
export TERM=xterm


# Display the values of the environment variables
echo_color "Displaying the values of the environment variables:" "blue"
echo "LIBVA_DRIVER_NAME: ${LIBVA_DRIVER_NAME}"
echo "GST_PLUGIN_PATH: ${GST_PLUGIN_PATH}"
echo "LD_LIBRARY_PATH: ${LD_LIBRARY_PATH}"
echo "LIBVA_DRIVERS_PATH: ${LIBVA_DRIVERS_PATH}"
echo "GST_VA_ALL_DRIVERS: ${GST_VA_ALL_DRIVERS}"
echo "MODEL_PROC_PATH: ${MODEL_PROC_PATH}"
echo "PYTHONPATH: ${PYTHONPATH}"
echo "TERM: ${TERM}"


if gst-inspect-1.0 gvadetect &> /dev/null; then
    echo_color " DL Streamer verification successful" "green"
else
    handle_error " DL Streamer verification failed"
    exit 1
fi
