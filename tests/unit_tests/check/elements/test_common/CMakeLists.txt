# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "test_common")

if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

find_package(OpenCV REQUIRED core imgproc imgcodecs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTCHECK gstreamer-check-1.0 REQUIRED)
pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTAUDIO gstreamer-audio-1.0>=1.16 REQUIRED)

file (GLOB MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    )

file (GLOB MAIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    )

add_library(${TARGET_NAME} SHARED ${MAIN_HEADERS} ${MAIN_SRC})

target_include_directories(${TARGET_NAME}
PUBLIC
    ${GSTCHECK_INCLUDE_DIRS}
    ${GSTVIDEO_INCLUDE_DIRS}
    ${GSTAUDIO_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${TARGET_NAME}
PUBLIC
    ${GSTCHECK_LIBRARIES}
    ${OpenCV_LIBS}
    gtest
    utils
PRIVATE
    ${GSTVIDEO_LIBRARIES}
    ${GSTAUDIO_LIBRARIES}
    gstvideoanalyticsmeta
)
