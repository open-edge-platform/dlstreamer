# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(PRODUCT_COMPANY_NAME "Intel Corporation")
set(PRODUCT_NAME "Deep Learning Streamer")
set(PRODUCT_COPYRIGHT "Copyright (C) 2018-2026 Intel Corporation")

# This function generates a version resource (.rc) file from a template and adds it to the given target.
function(add_vs_version_resource TARGET_NAME)
    set(VS_VERSION_TEMPLATE "${PROJECT_SOURCE_DIR}/cmake/vs_version.rc.in")
    set(VS_VERSION_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/vs_version.rc")

    set(PRODUCT_DESCRIPTION "${PRODUCT_NAME} ${TARGET_NAME} plugin")

    configure_file("${VS_VERSION_TEMPLATE}" "${VS_VERSION_OUTPUT}" @ONLY)

    target_sources(${TARGET_NAME} PRIVATE "${VS_VERSION_OUTPUT}")
endfunction()
