# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "test_gstvideoanalyticsmeta")

find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTCHECK gstreamer-check-1.0 REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GLIB2 glib-2.0 REQUIRED)

project(${TARGET_NAME})

set(TEST_SOURCES
    main_test.cpp
    video_frame_test.cpp
    region_of_interest_test.cpp
    tensor_test.cpp
    tensor_convert_test.cpp
    keypoint_descriptor_test.cpp
)

add_executable(${TARGET_NAME} ${TEST_SOURCES})

target_link_libraries(${TARGET_NAME}
PRIVATE
    gtest
    gmock
    inference_backend
    inference_elements
    gstvideoanalyticsmeta
    ${GSTREAMER_LIBRARIES}
    ${GSTCHECK_LIBRARIES}
    ${GLIB2_LIBRARIES}
)
target_include_directories(${TARGET_NAME}
PRIVATE
    ${GSTCHECK_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GLIB2_INCLUDE_DIRS}
    ${DLSTREAMER_BASE_DIR}/src/utils
)

add_test(NAME GSTVIDEOANALYTICSMETA_TEST COMMAND ${TARGET_NAME})
