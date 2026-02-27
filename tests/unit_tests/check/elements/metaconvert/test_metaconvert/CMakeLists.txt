# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "test_metaconvert_plugin_converting")

add_executable(${TARGET_NAME} gvametaconvert.cpp)

pkg_check_modules(GSTRTP gstreamer-rtp-1.0 REQUIRED)

target_include_directories(${TARGET_NAME}
PRIVATE
    ${DLSTREAMER_BASE_DIR}/src/monolithic/gst/elements/gvametaconvert
    ${GSTRTP_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
PRIVATE
    gstvideoanalyticsmeta
    test_common
    test_utils
    json-hpp
    ${GSTRTP_LIBRARIES}
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

