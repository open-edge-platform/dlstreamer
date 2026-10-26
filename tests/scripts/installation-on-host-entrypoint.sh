#!/bin/bash
# ==============================================================================
# Copyright (C) 2024-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
set -e

DEB_PKGS_PATH=$1
# Optional override for the OpenVINO GenAI version installed alongside the
# .deb (see install_openvino() below). When omitted, the hardcoded default
# below is used - keep it in sync with OPENVINO_GENAI_VERSION in
# docker/ubuntu/ubuntuNN.Dockerfile whenever OpenVINO is bumped.
OPENVINO_GENAI_VERSION_OVERRIDE=$2
PREREQUISITES_SCRIPT_PATH="$(dirname "$0")/../../scripts"

# Hardcoded default for the OpenVINO GenAI archive, which already bundles the
# full OpenVINO C++ runtime (same layout and setupvars.sh as the plain
# OpenVINO archive) - keep in sync with OPENVINO_GENAI_VERSION in
# docker/ubuntu/ubuntuNN.Dockerfile whenever OpenVINO is bumped.
DEFAULT_OPENVINO_GENAI_VERSION=2026.5.0.0.dev20261023

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

# Install OpenVINO GenAI (which bundles the full OpenVINO C++ runtime)
# alongside the intel-dlstreamer .deb (which no longer bundles OpenVINO, see
# docker/ubuntu/ubuntuNN.Dockerfile "dlstreamer" stage), using the exact
# archive version the .deb was built against, and register its library
# directory with ldconfig so DL Streamer's GStreamer plugins (no RPATH,
# default linker search path only) can resolve
# libopenvino*/libopenvino_genai* at runtime.
install_openvino() {
    . /etc/os-release
    local ubuntu_major
    case "${VERSION_CODENAME}" in
        jammy) ubuntu_major=22 ;;
        noble) ubuntu_major=24 ;;
        resolute) ubuntu_major=26 ;;
        *) handle_error "Ubuntu version ${VERSION_CODENAME} not supported" ;;
    esac

    local openvino_genai_version="${OPENVINO_GENAI_VERSION_OVERRIDE:-$DEFAULT_OPENVINO_GENAI_VERSION}"

    echo_color "Installing OpenVINO GenAI ${openvino_genai_version} alongside intel-dlstreamer..." "blue"

    sudo mkdir -p /opt/intel

    local openvino_genai_ver="openvino_genai_ubuntu${ubuntu_major}_${openvino_genai_version}_x86_64"
    local openvino_genai_pkg="https://storage.openvinotoolkit.org/repositories/openvino_genai/packages/nightly/${openvino_genai_version}/${openvino_genai_ver}.tar.gz"
    local genai_version_marker=/opt/intel/openvino_genai/.installed_version

    if [ -d /opt/intel/openvino_genai ] && [ "$(cat "$genai_version_marker" 2>/dev/null)" != "$openvino_genai_version" ]; then
        sudo rm -rf /opt/intel/openvino_genai
    fi
    if [ ! -d /opt/intel/openvino_genai ]; then
        local tmp_genai_dir
        tmp_genai_dir=$(mktemp -d)
        curl -fL "${openvino_genai_pkg}" | tar -xz -C "$tmp_genai_dir"
        sudo mv "${tmp_genai_dir}/${openvino_genai_ver}" /opt/intel/openvino_genai
        rm -rf "$tmp_genai_dir"
        echo "$openvino_genai_version" | sudo tee "$genai_version_marker" > /dev/null
    fi
    sudo ln -sfn /opt/intel/openvino_genai /opt/intel/openvino

    echo "/opt/intel/openvino/runtime/lib/intel64" | sudo tee /etc/ld.so.conf.d/openvino.conf > /dev/null
    sudo ldconfig

    echo_color "✓ OpenVINO installed and registered with ldconfig" "green"
}

cleanup_old_gpu_drivers() {
    echo_color "🧹 Cleaning up old GPU drivers and libraries..." "yellow"
    
    # List of packages to remove (old versions and conflicting packages)
    local packages_to_remove=(
        "intel-metrics-discovery"
        "intel-gsc" 
        "libvpl2"
        "libze-intel-gpu1"
        "libze1"
        "intel-opencl-icd"
        "clinfo"
        "intel-media-va-driver-non-free"
        "libmfx-gen1"
        "libvpl-tools"
        "libva-glx2"
        "va-driver-all"
        "vainfo"
        "level-zero"
        "intel-level-zero-gpu"
        "compute-runtime"
        "intel-gmmlib"
        "intel-igc-core"
        "intel-igc-cm"
        "intel-ocloc"
    )
    
    echo "Removing old GPU driver packages..."
    for package in "${packages_to_remove[@]}"; do
        if dpkg -l | grep -q "^ii.*$package"; then
            echo "Removing $package..."
            $SUDO_PREFIX apt-get remove -y "$package" 2>/dev/null || true
        fi
    done
    
    echo_color "✓ GPU drivers cleanup completed successfully!" "green"
}

# Stop and remove all running Docker containers
echo_color "Stopping all running Docker containers" "blue"
docker ps -q | xargs -r docker stop || true

# List remaining Docker containers
echo_color "Listing all remaining Docker containers" "blue"
docker ps -a

#Remove gstreamer .cache
echo_color "Checking $HOME/.cache/gstreamer-1.0/..." "blue"
if [ -f "$HOME/.cache/gstreamer-1.0/" ]; then
    sudo rm -rf "$HOME/.cache/gstreamer-1.0/"
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

for file in /etc/apt/sources.list.d/intel-openvino*.list; do
    if [ -f "$file" ]; then
        sudo rm -rf "$file"
        echo "Removed $file" "blue"
    fi
done

if [ -f /etc/apt/sources.list.d/sed.list ]; then
    sudo rm -rf /etc/apt/sources.list.d/sed.list
    echo_color "Removed /etc/apt/sources.list.d/sed.list" "blue"
fi

for file in /usr/share/keyrings/intel-graphics*; do
    if [ -f "$file" ]; then
        sudo rm -rf "$file"
    fi
done

# Call this function before installing new drivers
cleanup_old_gpu_drivers

chmod +x "$PREREQUISITES_SCRIPT_PATH"/DLS_install_prerequisites.sh
"$PREREQUISITES_SCRIPT_PATH"/DLS_install_prerequisites.sh --reinstall-npu-driver=yes

# OpenVINO is no longer bundled inside the intel-dlstreamer package, so it must
# be installed separately, alongside the .deb, before DL Streamer is used.
install_openvino

#DLStreamer installation
echo_color "Executing: sudo apt update" "blue"
sudo apt-get update
echo_color  "Completed: sudo apt update" "magenta"

echo_color "Executing: sudo apt install -y ./intel-dlstreamer*" "blue"
cd "$DEB_PKGS_PATH"
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
# shellcheck source=/dev/null
source /opt/intel/dlstreamer/scripts/setup_dls_env.sh
export PATH=/python3venv/bin:/opt/intel/dlstreamer/build/bin:$PATH
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
fi
