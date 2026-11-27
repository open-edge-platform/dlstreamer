# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "test_calibration_symlink")

project(${TARGET_NAME})

set(TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_calibration_symlink.cpp
    ${CMAKE_SOURCE_DIR}/src/monolithic/gst/3d_elements/g3dobjectfuser/calibration.cpp
)

add_executable(${TARGET_NAME} ${TEST_SOURCES})

target_include_directories(${TARGET_NAME}
PRIVATE
    ${CMAKE_SOURCE_DIR}/src/monolithic/gst/3d_elements/g3dobjectfuser
)

target_link_libraries(${TARGET_NAME}
PRIVATE
    gtest
    gmock
    json-hpp
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
