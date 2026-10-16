# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "test_audio")

find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTCHECK gstreamer-check-1.0 REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTAUDIO gstreamer-audio-1.0>=1.16 REQUIRED)
pkg_check_modules(GLIB2 glib-2.0 REQUIRED)

project(${TARGET_NAME})

set(TEST_SOURCES
    main_test.cpp
    audio_frame_test.cpp
    audio_event_test.cpp
    gva_audio_event_meta_test.cpp
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
    ${GSTAUDIO_LIBRARIES}
    ${GLIB2_LIBRARIES}
)
target_include_directories(${TARGET_NAME}
PRIVATE
    ${GSTCHECK_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GLIB2_INCLUDE_DIRS}
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
