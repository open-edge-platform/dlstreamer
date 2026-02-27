#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# Source download Script for Intel DL Streamer and it's dependencies RPM package generation
set -ex


# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Source versions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "$SCRIPT_DIR/versions.env" ]]; then
    source "$SCRIPT_DIR/versions.env"
else
    echo "[ERROR] versions.env not found in $SCRIPT_DIR" >&2
    exit 1
fi

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

download_file() {
    local url="$1"
    local output="$2"
    
    if [[ -f "$output" ]]; then
        log_warn "File $output already exists, skipping..."
        return 0
    fi
    
    log_info "Downloading $output..."
    if command -v wget &> /dev/null; then
        wget -O "$output" "$url"
    elif command -v curl &> /dev/null; then
        curl -L -o "$output" "$url"
    else
        log_error "Neither wget nor curl found. Please install one of them."
        exit 1
    fi
}

main() {
    log_info "Downloading source files for Intel DL Streamer modular build..."
    log_info "================================================================="
    
    # Define sources
    declare -A sources=(
        ["https://github.com/eclipse/paho.mqtt.c/archive/v${PAHO_MQTT_VERSION}.tar.gz"]="paho.mqtt.c-${PAHO_MQTT_VERSION}.tar.gz"
        ["https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.gz"]="ffmpeg-${FFMPEG_VERSION}.tar.gz"
        ["https://github.com/opencv/opencv/archive/${OPENCV_VERSION}.tar.gz"]="opencv-${OPENCV_VERSION}.tar.gz"
        ["https://gitlab.freedesktop.org/gstreamer/gstreamer/-/archive/${GSTREAMER_VERSION}/gstreamer-${GSTREAMER_VERSION}.tar.gz"]="gstreamer-${GSTREAMER_VERSION}.tar.gz"
    )
    
    # Download each source
    for url in "${!sources[@]}"; do
        download_file "$url" "${sources[$url]}"
    done

    # Download the DL Streamer src code
    cd ../../..
    git submodule update --init libraries/dl-streamer/thirdparty/spdlog
    cd libraries
    rm -rf ~/intel-dlstreamer-${DLSTREAMER_VERSION}*
    cp -r dl-streamer ~
    mv ~/dl-streamer ~/intel-dlstreamer-${DLSTREAMER_VERSION}
    cd ~
    tar czf intel-dlstreamer-${DLSTREAMER_VERSION}.tar.gz intel-dlstreamer-${DLSTREAMER_VERSION}
    cd -
    mv ~/intel-dlstreamer-${DLSTREAMER_VERSION}.tar.gz dl-streamer/SPECS/
    log_info ""
}

main "$@"
