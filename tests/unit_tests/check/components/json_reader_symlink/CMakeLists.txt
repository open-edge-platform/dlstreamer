# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "test_json_reader_symlink")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GLIB2 glib-2.0 REQUIRED)

project(${TARGET_NAME})

set(TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_json_reader_symlink.cpp
    ${CMAKE_SOURCE_DIR}/src/gst/bins/model_proc/json_reader.cpp
)

add_executable(${TARGET_NAME} ${TEST_SOURCES})

target_include_directories(${TARGET_NAME}
PRIVATE
    ${CMAKE_SOURCE_DIR}/src/gst/bins/model_proc
    ${GSTREAMER_INCLUDE_DIRS}
    ${GLIB2_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
PRIVATE
    gtest
    gmock
    ${GSTREAMER_LIBRARIES}
    ${GLIB2_LIBRARIES}
    json-hpp
    json-schema-validator
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
