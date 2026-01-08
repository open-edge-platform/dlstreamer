#!/bin/bash
# ==============================================================================
# Copyright (C) 2024-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
set -e
dls_package=$1

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
sudo docker system prune -a -f

# List remaining Docker containers
echo_color "Listing all remaining Docker containers" "blue"
docker ps -a

#Remove gstreamer .cache
if [ -f /home/labrat/.cache/gstreamer-1.0/ ]; then
    sudo rm -rf /home/labrat/.cache/gstreamer-1.0/
    echo_color "Removed /home/labrat/.cache/gstreamer-1.0/" "blue"
fi

# Configure repositories before installation
echo_color "Starting to configure OpenVINO™ repository access before DL Streamer installation" "blue"
printf "[OpenVINO]\n\
name=Intel(R) Distribution of OpenVINO\n\
baseurl=https://yum.repos.intel.com/openvino\n\
enabled=1\n\
gpgcheck=1\n\
repo_gpgcheck=1\n\
gpgkey=https://yum.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB\n" >/tmp/openvino.repo && \
sudo mv /tmp/openvino.repo /etc/yum.repos.d
. /etc/os-release
 
#DLStreamer installation

mkdir /home/labrat/intel/dls_package
cd /home/labrat/intel/dls_package

echo_color "Downloading DLS rpm package version: $dls_package" "blue"
wget -r -np -nH --cut-dirs=7 -A '*intel-dlstreamer*' "$dls_package"
echo_color "Downloaded DLS package version: $dls_package" "magenta"

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
export LIBVA_DRIVER_NAME=iHD
export GST_PLUGIN_PATH=/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/gstreamer/lib/gstreamer-1.0:/opt/intel/dlstreamer/gstreamer/lib/
export LD_LIBRARY_PATH=/opt/intel/dlstreamer/gstreamer/lib:/opt/intel/dlstreamer/lib:/opt/intel/dlstreamer/lib/gstreamer-1.0:/usr/lib:/opt/intel/dlstreamer/lib:/opt/opencv:/opt/rdkafka:/opt/ffmpeg:/usr/local/lib
export LIBVA_DRIVERS_PATH=/usr/lib64/dri-nonfree
export GST_VA_ALL_DRIVERS=1
export MODEL_PROC_PATH=/opt/intel/dlstreamer/samples/gstreamer/model_proc
export PATH=/python3venv/bin:/opt/intel/dlstreamer/gstreamer/bin:/opt/intel/dlstreamer/bin:$PATH
export PYTHONPATH=/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages:/home/dlstreamer/dlstreamer/python:/opt/intel/dlstreamer/gstreamer/lib/python3/dist-packages
export TERM=xterm
export GI_TYPELIB_PATH=/opt/intel/dlstreamer/gstreamer/lib/girepository-1.0:/usr/lib64/girepository-1.0


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
