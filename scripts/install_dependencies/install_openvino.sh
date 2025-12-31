#!/bin/bash -e
# ==============================================================================
# Copyright (C) 2022-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set -eo pipefail

# shellcheck source=/dev/null
. /etc/os-release
if [[ "$ID" == "ubuntu" && "$VERSION_ID" == "24.04" ]]; then
    echo "Ubuntu 24.04 detected"
    OS="ubuntu24"
    update_cmd=(apt-get update)
    install_cmd=(apt-get install -y)
    DEBIAN_FRONTEND=noninteractive
elif [[ "$ID" == "ubuntu" && "$VERSION_ID" == "22.04" ]]; then
    echo "Ubuntu 22.04 detected"
    OS="ubuntu22"
    update_cmd=(apt-get update)
    install_cmd=(apt-get install -y)
    DEBIAN_FRONTEND=noninteractive
elif [[ "$ID" == "rhel" && "$VERSION_ID" == 8* ]]; then
    echo "RHEL 8 detected"
    OS="rhel8"
    update_cmd=()
    install_cmd=(dnf install -y)
elif [[ "$ID" == "fedora" && "$VERSION_ID" == "41" ]]; then
    echo "Fedora 41 detected"
    OS="rhel8"
    update_cmd=()
    install_cmd=(dnf install -y)
else
    echo "Unsupported system: $ID $VERSION_ID"
    exit 1
fi


RUN_PREFIX=

OV_REMOTE_ARCHIVE_PATH="https://storage.openvinotoolkit.org/repositories/openvino/packages/2025.4/linux/openvino_toolkit_${OS}_2025.4.0.20398.8fdad55727d_x86_64.tgz"
OV_ARCHIVE_EXT=".tgz"
OV_CHECKSUM_EXT=".tgz.sha256"
OV_LOCAL_ARCHIVE_PATH=/tmp/openvino_installation

EXTRA_PYPI_INDEX_URL=
INSTALL_DEV_TOOLS=
BUILD_SAMPLES=
OV_UNINSTALL=

get_options() {
    while :; do
        case $1 in
        -h | -\? | --help)
            show_help
            exit
            ;;
        --remote-path)
            if [ "$2" ]; then
                OV_REMOTE_ARCHIVE_PATH=$2
                shift
            else
                error 'ERROR: "--remote-path" requires an argument.'
            fi
            ;;
        --extra-pypi-index)
            if [ "$2" ]; then
                EXTRA_PYPI_INDEX_URL=$2
                shift
            else
                error 'ERROR: "--extra-pypi-index" requires an argument.'
            fi
            ;;
        --install-dev-tools)
            INSTALL_DEV_TOOLS="true"
            ;;
        --include-samples)
            BUILD_SAMPLES="true"
            ;;
        --uninstall)
            OV_UNINSTALL="true"
            ;;
        --dry-run)
            RUN_PREFIX="echo"
            echo ""
            echo "=============================="
            echo "DRY RUN: COMMANDS PRINTED ONLY"
            echo "=============================="
            echo ""
            ;;
        --)
            shift
            break
            ;;
        -?*)
        error 'ERROR: Unknown option: ' "$1"
            ;;
        ?*)
        error 'ERROR: Unknown option: ' "$1"
            ;;
        *)
            break
            ;;
        esac

        shift
    done
}

show_help() {
    echo "usage: install_openvino.sh"
    echo "  [--remote-path  archive_server_url] : external or internal network hosting OpenVINO™ tarball."
    echo "  [--install-dev-tools] : installs OpenVINO™ development tools."
    echo "  [--extra-pypi-index additional_pypi_url]: used in combination with --install-dev-tools for internal url hosting pypi packages."
    echo "  [--devtools-version explicit_openvino-dev_version]: used in combination with --install-dev-tools."
    echo "  [--include-samples] : builds OpenVINO™ 2022.2 samples during installation process."
    echo "  [--dry-run print commands without execution.]"
    exit 0
}

error() {
    printf '%s %s\n' "$1" "$2" >&2
    exit 1
}

get_options "$@"

# ==============================================================================
# ERROR CHECKS
# ==============================================================================
if [ "$EUID" -ne 0 ]; then
    error "ERROR: Must run as root user (use 'sudo -E ./install_openvino.sh ')."
fi
if [ -d "${OV_INSTALL_PATH}" ]; then
    if [ "$OV_UNINSTALL" != "true" ]; then
        echo "OpenVINO™ installation folder already exists!  You must first uninstall before running this installation script."
        if [ "$RUN_PREFIX" != "echo" ]; then
            exit 1
        else
            echo "Intentionally resuming for dry-run expansion of install commands:"
        fi
    fi
fi

# ==============================================================================
# DOWNLOAD, VERIFY, EXTRACT
# ==============================================================================
$RUN_PREFIX mkdir -p $OV_LOCAL_ARCHIVE_PATH
$RUN_PREFIX "${update_cmd[@]}"
$RUN_PREFIX "${install_cmd[@]}" wget

# Download OpenVINO™ archive and .sha256 files to temp folder
$RUN_PREFIX wget "${OV_REMOTE_ARCHIVE_PATH}" -A "*_${OS}_*${OV_ARCHIVE_EXT},*_${OS}_*${OV_CHECKSUM_EXT}" -P ${OV_LOCAL_ARCHIVE_PATH} -r -l1 -nd -np -e robots=off -U mozilla

# Verify contents, extract and move to install folder
$RUN_PREFIX pushd $OV_LOCAL_ARCHIVE_PATH

$RUN_PREFIX sha256sum -c ./*"${OV_CHECKSUM_EXT}" >& checksum_results || true
if grep -q "No such file or directory" checksum_results; then
    echo "checksum file not found .. continuing"
elif grep -q "OK" checksum_results; then
    echo "Checksum is identical"
elif grep -q "FAILED" checksum_results; then
    echo "Checksum MISMATCH"
    exit 1
fi

OV_SHORT_VERSION=$(echo "$OV_REMOTE_ARCHIVE_PATH" | grep -oP '(?<=_)[0-9]{4}\.[0-9]+')
OV_YEAR=$(echo "$OV_SHORT_VERSION" | awk -F'.' '{print $1}')
OV_INSTALL_MOUNT=/opt/intel/openvino_${OV_YEAR}
OV_SAMPLES_PATH=${OV_INSTALL_MOUNT}/samples

# Handle requests for uninstall
if [ "$OV_UNINSTALL" == "true" ]; then
    # Dynamically identify the first version encountered and allow user to confirm uninstall
    if ls "/opt/intel/openvino_${OS}_*" 1> /dev/null 2>&1; then
        OPENVINO_VERSION=$(ls -d "/opt/intel/openvino_${OS}_*") && OPENVINO_VERSION=${OPENVINO_VERSION#*openvino_}
        OV_INSTALL_PATH="/opt/intel/openvino_${OPENVINO_VERSION}"
    else
        echo "WARNING: No installation of OpenVINO™ was found."
    fi
    if [ ! -d "${OV_INSTALL_PATH}" ]; then
        echo "Could not detect any version of OpenVINO™ to uninstall."
        exit 1
    fi
    echo "Remove OpenVINO™ installed at: ${OV_INSTALL_PATH}"
    read -p "Are you sure (Y/n)? " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Exiting script"
        exit 1
    fi
    echo "Removing install path at: ${OV_INSTALL_PATH}"
    $RUN_PREFIX rm -rf "${OV_INSTALL_PATH}"
    echo "Removing symlink at: ${OV_INSTALL_MOUNT}"
    $RUN_PREFIX unlink "${OV_INSTALL_MOUNT}"
    echo "Uninstall complete."
    exit 0
fi

echo "Extracting files from $(ls ./*${OV_ARCHIVE_EXT})"
echo "Creating dynamic installation"
OV_INSTALL_PATH="/opt/intel/openvino_${OV_SHORT_VERSION}"

echo "Installing to ${OV_INSTALL_PATH}..."
$RUN_PREFIX mkdir -p "${OV_INSTALL_PATH}"
$RUN_PREFIX tar -xvf ./*${OV_ARCHIVE_EXT} -C "${OV_INSTALL_PATH}" --strip-components=1

$RUN_PREFIX popd
$RUN_PREFIX rm -r "$OV_LOCAL_ARCHIVE_PATH"

# Add/update symlink so /opt/intel/openvino_$OV_YEAR points to the install folder
$RUN_PREFIX ln -sfn "$OV_INSTALL_PATH" "$OV_INSTALL_MOUNT"

# ==============================================================================
# POST-INSTALLATION
# ==============================================================================
if ! $RUN_PREFIX "${OV_INSTALL_PATH}"/install_dependencies/install_openvino_dependencies.sh -y ; then
    echo "WARNING: OpenVINO™ does not support installing dependencies on this OS."
    echo "Temporarily installing Ubuntu 20.04 dependencies for preview support."
    if ! os=ubuntu20.04 "${OV_INSTALL_PATH}"/install_dependencies/install_openvino_dependencies.sh -y ; then
        echo "ERROR: could not install OpenVINO™ dependencies on this OS."
    fi
fi
if [ "$BUILD_SAMPLES" == "true" ]; then
    # NOTE: This currently locates samples beneath:
    # /opt/intel/openvino_$OV_YEAR/samples/samples_bin
    $RUN_PREFIX "${OV_INSTALL_PATH}"/samples/cpp/build_samples.sh -i "${OV_SAMPLES_PATH}"
fi

if [ "$INSTALL_DEV_TOOLS" == "true" ]; then
    export DEBIAN_FRONTEND=noninteractive
    $RUN_PREFIX "${update_cmd[@]}"
    $RUN_PREFIX "${install_cmd[@]}" python3-pip python3-sympy && pip3 install --upgrade pip setuptools
    if [ -n "$EXTRA_PYPI_INDEX_URL" ]; then
        $RUN_PREFIX python3 -m pip config set global.extra-index-url "${EXTRA_PYPI_INDEX_URL}"
    fi
    if [ -n "$OV_SHORT_VERSION" ]; then
        echo "Installing explicitly requested developer tools version ${OV_SHORT_VERSION}..."
        $RUN_PREFIX python3 -m pip install 'openvino-dev[onnx,tensorflow,tensorflow2,pytorch,mxnet,kaldi,caffe]'=="${OV_SHORT_VERSION}"
    else
        $RUN_PREFIX python3 -m pip install 'openvino-dev[onnx,tensorflow,tensorflow2,pytorch,mxnet,kaldi,caffe]'
    fi
fi

# Additional steps may be performed outside of this script. 
#$RUN_PREFIX $OV_INSTALL_PATH/install_dependencies/install_NEO_OCL_driver.sh
