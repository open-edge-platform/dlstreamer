# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# Helper Script to Refresh GStreamer Source Hashes
# Executed with cmake -P after bumping GSTREAMER_VERSION. Downloads the
# .sha256sum file published next to every source tarball and prints a
# ready-to-paste block for gstreamer_sources.cmake.

include("${CMAKE_CURRENT_LIST_DIR}/gstreamer_sources.cmake")

set(HASH_BLOCK "")

foreach(MODULE IN LISTS GSTREAMER_SOURCE_MODULES)
    set(SUMFILE_URL "${GSTREAMER_SOURCE_BASE_URL}/${MODULE}/${MODULE}-${GSTREAMER_VERSION}.tar.xz.sha256sum")
    set(SUMFILE "${CMAKE_CURRENT_BINARY_DIR}/${MODULE}.sha256sum")

    message(STATUS "Fetching ${SUMFILE_URL}")
    file(DOWNLOAD "${SUMFILE_URL}" "${SUMFILE}" STATUS DOWNLOAD_STATUS TIMEOUT 60)

    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    list(GET DOWNLOAD_STATUS 1 ERROR_MESSAGE)
    if(NOT STATUS_CODE EQUAL 0)
        file(REMOVE "${SUMFILE}")
        message(FATAL_ERROR "Failed to download ${SUMFILE_URL}: ${ERROR_MESSAGE}")
    endif()

    file(READ "${SUMFILE}" SUMFILE_CONTENT)
    file(REMOVE "${SUMFILE}")

    # CMake regex has no bounded repetition, so match greedily and check the length
    if(NOT SUMFILE_CONTENT MATCHES "^([0-9a-f]+) ")
        message(FATAL_ERROR "No SHA256 hash found in ${SUMFILE_URL}")
    endif()
    set(MODULE_HASH "${CMAKE_MATCH_1}")
    string(LENGTH "${MODULE_HASH}" MODULE_HASH_LENGTH)
    if(NOT MODULE_HASH_LENGTH EQUAL 64)
        message(FATAL_ERROR "Unexpected hash length ${MODULE_HASH_LENGTH} in ${SUMFILE_URL}")
    endif()

    # Pad the variable name so the generated block stays column-aligned
    string(LENGTH "${MODULE}" MODULE_NAME_LENGTH)
    set(PADDING "")
    while(MODULE_NAME_LENGTH LESS 20)
        string(APPEND PADDING " ")
        math(EXPR MODULE_NAME_LENGTH "${MODULE_NAME_LENGTH} + 1")
    endwhile()

    string(APPEND HASH_BLOCK "set(GSTREAMER_SRC_HASH_${MODULE}${PADDING} \"${MODULE_HASH}\")\n")
endforeach()

message("")
message("Paste the following into cmake/packaging/windows/gstreamer_sources.cmake:")
message("")
message("${HASH_BLOCK}")
