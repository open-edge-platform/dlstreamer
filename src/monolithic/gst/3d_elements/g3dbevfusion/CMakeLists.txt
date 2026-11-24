# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "g3dbevfusion")

find_package(PkgConfig REQUIRED)
find_package(OpenVINO REQUIRED Runtime)
find_package(OpenCV REQUIRED COMPONENTS core imgproc)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_BASE REQUIRED gstreamer-base-1.0)
pkg_check_modules(GSTANALYTICS gstreamer-analytics-1.0>=1.28 REQUIRED)
pkg_check_modules(GSTVIDEO REQUIRED gstreamer-video-1.0)

# Model runtime, independent of GStreamer.
add_library(g3dbevfusion_runtime STATIC
    bevfusion_preprocess.cpp
    bevfusion_runtime.cpp
)
set_compile_flags(g3dbevfusion_runtime)
target_include_directories(g3dbevfusion_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(g3dbevfusion_runtime
    PUBLIC
        openvino::runtime
    PRIVATE
        ${OpenCV_LIBS}
        json-hpp
)

add_library(${TARGET_NAME} STATIC
    g3dbevfusion.cpp
)
set_compile_flags(${TARGET_NAME})
target_include_directories(${TARGET_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${DLSTREAMER_BASE_DIR}/include
        ${DLSTREAMER_BASE_DIR}/include/dlstreamer/gst/metadata
    PRIVATE
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTREAMER_BASE_INCLUDE_DIRS}
        ${GSTVIDEO_INCLUDE_DIRS}
        ${GSTANALYTICS_INCLUDE_DIRS}
)
target_link_libraries(${TARGET_NAME}
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTREAMER_BASE_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
        ${GSTANALYTICS_LIBRARIES}
        g3dbevfusion_runtime
        json-hpp
        dlstreamer_gst_meta
)
