#!/usr/bin/env bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# Build wheel package for onvif-cameras-discovery.
#
# Usage:
#   ./build_dls_onvif_sample_whl.sh              # build only
#   ./build_dls_onvif_sample_whl.sh --clean       # remove artifacts, then build
#   ./build_dls_onvif_sample_whl.sh --uninstall   # uninstall the package
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="${SCRIPT_DIR}/dist"
PACKAGE_NAME="onvif-cameras-discovery"

# ---- helpers ----------------------------------------------------------------
info()  { printf "\033[1;34m[INFO]\033[0m  %s\n" "$*"; }
error() { printf "\033[1;31m[ERROR]\033[0m %s\n" "$*" >&2; }

# ---- uninstall --------------------------------------------------------------
if [[ "${1:-}" == "--uninstall" ]]; then
    if pip show "${PACKAGE_NAME}" &>/dev/null; then
        info "Uninstalling ${PACKAGE_NAME} …"
        pip uninstall -y "${PACKAGE_NAME}"
        info "${PACKAGE_NAME} uninstalled."
    else
        info "${PACKAGE_NAME} is not installed."
    fi
    exit 0
fi

# ---- clean (optional) -------------------------------------------------------
if [[ "${1:-}" == "--clean" ]]; then
    info "Cleaning previous build artifacts …"
    rm -rf "${DIST_DIR}" "${SCRIPT_DIR}/build" "${SCRIPT_DIR}"/*.egg-info
fi

# ---- preflight checks -------------------------------------------------------
if ! command -v python3 &>/dev/null; then
    error "python3 not found in PATH"
    exit 1
fi

info "Using Python: $(python3 --version 2>&1)"

# ---- ensure build tools -----------------------------------------------------
info "Upgrading pip, setuptools, wheel …"
python3 -m pip install --upgrade pip setuptools wheel --quiet

# ---- build -------------------------------------------------------------------
info "Building wheel …"
mkdir -p "${DIST_DIR}"
pip wheel --no-deps -w "${DIST_DIR}" "${SCRIPT_DIR}"

# ---- result ------------------------------------------------------------------
WHL_FILE=$(find "${DIST_DIR}" -name "*.whl" -type f | head -1)
if [[ -z "${WHL_FILE}" ]]; then
    error "No .whl file found in ${DIST_DIR}"
    exit 1
fi

info "Wheel built successfully:"
info "  ${WHL_FILE}"
info ""
info "Install with:"
info "  pip install ${WHL_FILE}"
