#!/bin/bash
# ==============================================================================
# Copyright (C) 2024-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
set -e

DEB_PKGS_PATH=$1
PREREQUISITES_SCRIPT_PATH="$(dirname "$0")/../../scripts"

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

# Stop and remove all running Docker containers
echo_color "Stopping all running Docker containers" "blue"
docker ps -q | xargs -r docker stop || true
echo_color "Removing all stopped Docker containers" "blue"
docker ps -a -q | xargs -r docker rm || true
docker system prune -a -f

# List remaining Docker containers
echo_color "Listing all remaining Docker containers" "blue"
docker ps -a

#Remove gstreamer .cache
echo_color "Checking $HOME/.cache/gstreamer-1.0/..." "blue"
if [ -f $HOME/.cache/gstreamer-1.0/ ]; then
    sudo rm -rf $HOME/.cache/gstreamer-1.0/
    echo_color "Removed $HOME/.cache/gstreamer-1.0/" "blue"
fi

#Remove repos if they exist
echo_color "Checking $HOME/.cache/gstreamer-1.0/..." "blue"
if [ -f /etc/apt/sources.list.d/intel-graphics.list ]; then
    sudo rm -rf /etc/apt/sources.list.d/intel-graphics.list
    echo_color "Removed /etc/apt/sources.list.d/intel-graphics.list" "blue"
fi

if [ -f /etc/apt/sources.list.d/intel-gpu-jammy.list ]; then
    sudo rm -rf /etc/apt/sources.list.d/intel-gpu-jammy.list
    echo_color "Removed /etc/apt/sources.list.d/intel-gpu-jammy.list" "blue"
fi

if [ -f /etc/apt/sources.list.d/intel-openvino-2024.list ]; then
    sudo rm -rf /etc/apt/sources.list.d/intel-openvino-2024.list
    echo "Removed /etc/apt/sources.list.d/intel-openvino-2024.list" "blue"
fi

if [ -f /etc/apt/sources.list.d/intel-openvino-2025.list ]; then
    sudo rm -rf /etc/apt/sources.list.d/intel-openvino-2025.list
    echo "Removed /etc/apt/sources.list.d/intel-openvino-2025.list" "blue"
fi

if [ -f /etc/apt/sources.list.d/sed.list ]; then
    sudo rm -rf /etc/apt/sources.list.d/sed.list
    echo_color "Removed /etc/apt/sources.list.d/sed.list" "blue"
fi

for file in /usr/share/keyrings/intel-graphics*; do
    if [ -f "$file" ]; then
        sudo rm -rf "$file"
    fi
done

chmod +x "$PREREQUISITES_SCRIPT_PATH"/DLS_install_prerequisites.sh
"$PREREQUISITES_SCRIPT_PATH"/DLS_install_prerequisites.sh --reinstall-npu-driver=no

# Configure repositories before installation
echo_color "Starting to configure OpenVINO™ repository access before DL Streamer installation" "blue"
sudo -E wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB | gpg --dearmor | sudo tee /usr/share/keyrings/intel-gpg-archive-keyring.gpg > /dev/null

. /etc/os-release
if [[ ! " jammy noble " =~  ${VERSION_CODENAME}  ]]; then
    echo_color "Ubuntu version ${VERSION_CODENAME} not supported" "red"
else
    if [[ "${VERSION_CODENAME}" == "jammy" ]]; then
        sudo bash -c 'echo "deb [signed-by=/usr/share/keyrings/intel-gpg-archive-keyring.gpg] https://apt.repos.intel.com/openvino/2025 ubuntu22 main" | sudo tee /etc/apt/sources.list.d/intel-openvino-2025.list'
        echo_color "Completed to configure OpenVINO™ repository access before DL Streamer installation for Ubuntu 22" "magenta"
    fi
    if [[ "${VERSION_CODENAME}" == "noble" ]]; then
        sudo bash -c 'echo "deb [signed-by=/usr/share/keyrings/intel-gpg-archive-keyring.gpg] https://apt.repos.intel.com/openvino/2025 ubuntu24 main" | sudo tee /etc/apt/sources.list.d/intel-openvino-2025.list'
        echo_color "Completed to configure OpenVINO™ repository access before DL Streamer installation for Ubuntu 24" "magenta"
    fi
fi

echo_color "Executing: sudo apt update" "blue"
sudo apt update
echo_color  "Completed: sudo apt update" "magenta"

#DLStreamer installation
echo_color "Executing: sudo apt update" "blue"
sudo apt-get update
echo_color  "Completed: sudo apt update" "magenta"

echo_color "Executing: sudo apt install -y ./intel-dlstreamer*" "blue"
cd $DEB_PKGS_PATH
sudo apt install -y ./intel-dlstreamer*
echo_color "Completed: sudo apt install -y ./intel-dlstreamer*" "magenta"

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
export LIBVA_DRIVER_NAME=iHD
export GST_PLUGIN_PATH=/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0:/opt/intel/dlstreamer/gstreamer/lib/
export LD_LIBRARY_PATH=/opt/intel/dlstreamer/gstreamer/lib:/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/lib/gstreamer-1.0:/usr/lib:/opt/intel/dlstreamer/lib:/opt/opencv:/opt/openh264:/opt/rdkafka:/opt/ffmpeg:/usr/local/lib/gstreamer-1.0:/usr/local/lib
export LIBVA_DRIVERS_PATH=/usr/lib/x86_64-linux-gnu/dri
export GST_VA_ALL_DRIVERS=1
export PATH=/python3venv/bin:/opt/intel/dlstreamer/gstreamer/bin:/opt/intel/dlstreamer/build/bin:$PATH
export PYTHONPATH=/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:/home/dlstreamer/dlstreamer/python:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:
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
