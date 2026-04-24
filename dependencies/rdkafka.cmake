# ==============================================================================
# Copyright (C) 2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

include(ExternalProject)

# When changing version, you will also need to change the download hash
set(DESIRED_VERSION 2.12.1)

ExternalProject_Add(
    rdkafka
    PREFIX ${CMAKE_BINARY_DIR}/rdkafka
    URL     https://github.com/confluentinc/librdkafka/archive/v${DESIRED_VERSION}.tar.gz
    URL_MD5 86ed3acd2f9d9046250dea654cee59a8
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    TEST_COMMAND    ""
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/opencv-bin
)

if (INSTALL_DLSTREAMER)
    execute_process(COMMAND mkdir -p ${DLSTREAMER_INSTALL_PREFIX}/rdkafka
                    COMMAND cp -r ${CMAKE_BINARY_DIR}/rdkafka-bin/. ${DLSTREAMER_INSTALL_PREFIX}/rdkafka)
endif()
