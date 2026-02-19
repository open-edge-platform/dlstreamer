# ==============================================================================
# Copyright (C) 2018-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "test_metaconvert_plugin_converting")

add_executable(${TARGET_NAME} gvametaconvert.cpp)

target_include_directories(${TARGET_NAME}
PRIVATE
    ${DLSTREAMER_BASE_DIR}/src/monolithic/gst/elements/gvametaconvert
)

target_link_libraries(${TARGET_NAME}
PRIVATE
    gstvideoanalyticsmeta
    test_common
    test_utils
    json-hpp
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

