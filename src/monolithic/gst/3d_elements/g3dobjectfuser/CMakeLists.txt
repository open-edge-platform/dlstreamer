# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "g3dobjectfuser")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTBASE gstreamer-base-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTANALYTICS gstreamer-analytics-1.0>=1.28 REQUIRED)
find_package(OpenCV REQUIRED core imgproc)

add_library(${TARGET_NAME} STATIC
    g3dobjectfuser.cpp
    object_fuser_impl.cpp
    calibration.cpp
)
set_compile_flags(${TARGET_NAME})

if(UNIX)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=unused-variable -Wno-error=unused-parameter)
endif()

target_include_directories(${TARGET_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${DLSTREAMER_BASE_DIR}/include
    PRIVATE
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTBASE_INCLUDE_DIRS}
        ${GSTVIDEO_INCLUDE_DIRS}
        ${GSTANALYTICS_INCLUDE_DIRS}
        ${OpenCV_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/src/monolithic/gst/elements/gvatrack
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTBASE_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
        ${GSTANALYTICS_LIBRARIES}
        ${OpenCV_LIBS}
        gvatrack
        json-hpp
)
