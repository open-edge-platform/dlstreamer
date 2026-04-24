# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# Download Script for Installer Dependencies
# This file is executed with cmake -P to download dependencies

# Download URLs
set(GSTREAMER_INSTALLER_URL "https://gstreamer.freedesktop.org/data/pkg/windows/${GSTREAMER_VERSION}/msvc/gstreamer-1.0-msvc-x86_64-${GSTREAMER_VERSION}.exe")

# File paths
set(GSTREAMER_INSTALLER_FILE "${DLSTREAMER_DEPS_DIR}/gstreamer-1.0-msvc-x86_64-${GSTREAMER_VERSION}.exe")

# Download function
function(download_dependency URL DEST_FILE DESCRIPTION EXPECTED_HASH)
    if(NOT EXISTS "${DEST_FILE}")
        message(STATUS "Downloading ${DESCRIPTION}...")

        file(DOWNLOAD
            "${URL}"
            "${DEST_FILE}"
            SHOW_PROGRESS
            STATUS DOWNLOAD_STATUS
            TIMEOUT 900
            EXPECTED_HASH SHA256=${EXPECTED_HASH}
        )
        
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
        list(GET DOWNLOAD_STATUS 1 ERROR_MESSAGE)
        
        if(NOT STATUS_CODE EQUAL 0)
            file(REMOVE "${DEST_FILE}")
            message(FATAL_ERROR "Failed to download ${DESCRIPTION}: ${ERROR_MESSAGE}")
        else()
            message(STATUS "Successfully downloaded ${DESCRIPTION}")
        endif()
    else()
        file(SHA256 "${DEST_FILE}" FILE_HASH)
        if(NOT FILE_HASH STREQUAL EXPECTED_HASH)
            message(WARNING "Hash mismatch for ${DEST_FILE}, re-downloading...")
            file(REMOVE "${DEST_FILE}")
            download_dependency("${URL}" "${DEST_FILE}" "${DESCRIPTION}" "${EXPECTED_HASH}")
            return()
        endif()
        message(STATUS "${DESCRIPTION} already downloaded and verified: ${DEST_FILE}")
    endif()
endfunction()

# Download GStreamer
download_dependency(
    "${GSTREAMER_INSTALLER_URL}"
    "${GSTREAMER_INSTALLER_FILE}"
    "GStreamer ${GSTREAMER_VERSION}"
    "${GSTREAMER_INSTALLER_HASH}"
)

message(STATUS "=== All dependencies ready ===")
