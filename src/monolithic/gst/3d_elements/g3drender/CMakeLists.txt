# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "g3drender")

find_package(PkgConfig REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc calib3d)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_BASE REQUIRED gstreamer-base-1.0)
pkg_check_modules(GSTANALYTICS gstreamer-analytics-1.0>=1.28 REQUIRED)

add_library(${TARGET_NAME} STATIC
    g3drender.cpp
)
set_compile_flags(${TARGET_NAME})

if(WIN32)
    # MSVC does not define M_PI unless _USE_MATH_DEFINES is set before <cmath>
    target_compile_definitions(${TARGET_NAME} PRIVATE _USE_MATH_DEFINES)
endif()

if(UNIX)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=unused-variable -Wno-error=unused-parameter)
endif()

target_include_directories(${TARGET_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${DLSTREAMER_BASE_DIR}/include
        ${DLSTREAMER_BASE_DIR}/include/dlstreamer/gst/metadata
    PRIVATE
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTREAMER_BASE_INCLUDE_DIRS}
        ${GSTANALYTICS_INCLUDE_DIRS}
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTREAMER_BASE_LIBRARIES}
        ${GSTANALYTICS_LIBRARIES}
        ${OpenCV_LIBS}
        dlstreamer_gst_meta
)
