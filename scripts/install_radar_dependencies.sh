#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -e

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
THIRD_PARTY_BUILD_DIR="${SCRIPT_DIR}/../third_party_build"

mkdir -p "${THIRD_PARTY_BUILD_DIR}"

_install_oneapi()
{
  pushd "${THIRD_PARTY_BUILD_DIR}"
  curl -k -o GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB -L
  sudo -E apt-key add GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB && sudo rm GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB

  echo "deb https://apt.repos.intel.com/oneapi all main" | sudo tee /etc/apt/sources.list.d/oneAPI.list

  sudo -E apt-get update -y
  sudo -E apt-get install -y intel-oneapi-base-toolkit lsb-release
  popd
}

_install_libradar()
{
  pushd "${THIRD_PARTY_BUILD_DIR}"
  
  # Add the Intel SED repository key
  sudo -E wget -O- https://eci.intel.com/sed-repos/gpg-keys/GPG-PUB-KEY-INTEL-SED.gpg | sudo tee /usr/share/keyrings/sed-archive-keyring.gpg > /dev/null

  # Add the repository to the sources list
  echo "deb [signed-by=/usr/share/keyrings/sed-archive-keyring.gpg] https://eci.intel.com/sed-repos/$(source /etc/os-release && echo "$VERSION_CODENAME") sed main" | sudo tee /etc/apt/sources.list.d/sed.list
  echo "deb-src [signed-by=/usr/share/keyrings/sed-archive-keyring.gpg] https://eci.intel.com/sed-repos/$(source /etc/os-release && echo "$VERSION_CODENAME") sed main" | sudo tee -a /etc/apt/sources.list.d/sed.list

  # Set package pinning preferences
  sudo bash -c 'echo -e "Package: *\nPin: origin eci.intel.com\nPin-Priority: 1000" > /etc/apt/preferences.d/sed'

  # Update package list and install libradar
  sudo -E apt update
  sudo -E apt-get install -y libradar

  popd
}

main()
{
  echo "Installing Intel oneAPI..."
  _install_oneapi
  
  echo "Installing libradar..."
  _install_libradar

  echo "Sourcing oneAPI environment variables..."
  if [ -f "/opt/intel/oneapi/setvars.sh" ]; then
    source /opt/intel/oneapi/setvars.sh
    echo "oneAPI environment variables sourced successfully!"
  else
    echo "WARNING: /opt/intel/oneapi/setvars.sh not found."
    echo "Please ensure oneAPI is installed correctly."
  fi
  
  echo ""
  echo "Radar dependencies installation completed successfully!"
}

main "$@"
