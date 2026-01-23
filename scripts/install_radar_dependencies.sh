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

_create_env_setup_script()
{
  # Find libradar.so location
  LIBRADAR_PATH=$(dpkg -L libradar 2>/dev/null | grep -E "libradar\.so(\.[0-9]+)?$" | head -1 || echo "")
  
  if [ -z "$LIBRADAR_PATH" ]; then
    echo "WARNING: Could not locate libradar.so"
    return 1
  fi
  
  LIBRADAR_DIR=$(dirname "$LIBRADAR_PATH")
  echo "Found libradar at: $LIBRADAR_PATH"
  
  # Create environment setup script for LD_LIBRARY_PATH and source oneAPI environment
  ENV_SETUP_FILE="${SCRIPT_DIR}/../setup_radar_env.sh"
  cat > "$ENV_SETUP_FILE" << EOF
#!/bin/bash
# Radar Dependencies Environment Setup
# Source this file to set up environment variables for radar processing

# Add libradar to library path
export LD_LIBRARY_PATH="${LIBRADAR_DIR}:\${LD_LIBRARY_PATH}"

# Source oneAPI environment if available
if [ -f "/opt/intel/oneapi/setvars.sh" ]; then
  source /opt/intel/oneapi/setvars.sh --force
fi

echo "Radar environment variables configured!"
echo "  LD_LIBRARY_PATH includes: ${LIBRADAR_DIR}"
EOF
  
  chmod +x "$ENV_SETUP_FILE"
  echo ""
  echo "Environment setup script created at: $ENV_SETUP_FILE"
}

main()
{
  echo "Installing Intel oneAPI..."
  _install_oneapi
  
  echo "Installing libradar..."
  _install_libradar
  
  echo ""
  echo "Creating environment setup script..."
  _create_env_setup_script
  
  echo ""
  echo "========================================"
  echo "Radar dependencies installation completed successfully!"
  echo "========================================"
  echo ""
  echo "IMPORTANT: Before using g3dradarprocess, source the environment setup script:"
  echo "  source ${SCRIPT_DIR}/../setup_radar_env.sh"
  echo ""
}

main "$@"
