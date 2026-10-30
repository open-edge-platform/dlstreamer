#!/bin/bash
# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
set -e

PKG_MGR="${1:-apt}"

if [[ "$PKG_MGR" != "apt" && "$PKG_MGR" != "dnf" ]]; then
    echo "Unsupported package manager: $PKG_MGR. Use 'apt' or 'dnf'."
    exit 1
fi

#Uninstall DLStreamer
echo "Removing intel-dlstreamer package..."
sudo $PKG_MGR remove -y intel-dlstreamer
echo " Intel-dlstreamer package uninstalled..."
echo "Checking if there is any DLStreamer package installed"
if [ "$PKG_MGR" == "apt" ]; then
    installed_packages=$(sudo apt list --installed 2>/dev/null | grep dlstreamer || true)
elif [ "$PKG_MGR" == "dnf" ]; then
    installed_packages=$(sudo dnf list installed 2>/dev/null | grep dlstreamer || true)
fi

echo " Found packages: $installed_packages"

#Uninstall openvino packages
echo "Uninstall openvino packages"
sudo $PKG_MGR remove -y openvino* libopenvino* python3-openvino* || true
sudo $PKG_MGR autoremove -y
echo "Packages are uninstalled"

if [ "$PKG_MGR" == "apt" ]; then
    echo "Removing OpenVINO repository"
    sudo rm -rf /etc/apt/sources.list.d/intel-openvino*.list
    sudo rm -rf /etc/apt/sources.list.d/sed.list
elif [ "$PKG_MGR" == "dnf" ]; then
    echo "Removing OpenVINO repository for Fedora"
    sudo rm -rf /etc/yum.repos.d/intel-openvino*.repo
fi

if [ -n "$installed_packages" ]; then
    echo "Found the following dlstreamer packages installed:"
    echo "$installed_packages"
    exit 1
else
    echo "No dlstreamer packages found."
    exit 0
fi
