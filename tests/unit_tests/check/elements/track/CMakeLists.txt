# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "test_gvatrack_meta")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTCHECK gstreamer-check-1.0 REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTVIDEO gstreamer-video-1.0 REQUIRED)

add_executable(${TARGET_NAME} test_gvatrack_meta.cpp)

target_include_directories(${TARGET_NAME}
PRIVATE
    ${GSTCHECK_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTVIDEO_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
PRIVATE
    gtest
    gstvideoanalyticsmeta
    ${GSTCHECK_LIBRARIES}
    ${GSTREAMER_LIBRARIES}
    ${GSTVIDEO_LIBRARIES}
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
