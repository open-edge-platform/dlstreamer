# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "gvagenai")

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_BASE REQUIRED gstreamer-base-1.0)
pkg_check_modules(GSTREAMER_VIDEO REQUIRED gstreamer-video-1.0)
find_package(OpenVINOGenAI REQUIRED)
find_package(OpenCV REQUIRED)

# Add executable
add_library(${TARGET_NAME} STATIC
    gstgvagenai.cpp
    genai.cpp
    configs.cpp
)

# Include directories
target_include_directories(${TARGET_NAME} PUBLIC
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_BASE_INCLUDE_DIRS}
    ${GSTREAMER_VIDEO_INCLUDE_DIRS}
    ${OpenVINO_INCLUDE_DIRS}
    ${DLSTREAMER_BASE_DIR}/include/dlstreamer/gst/metadata
)

# Link libraries
target_link_libraries(${TARGET_NAME}
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_BASE_LIBRARIES}
    ${GSTREAMER_VIDEO_LIBRARIES}
    openvino::genai
    ${OpenCV_LIBS}
    common
    dlstreamer_gst_meta
    json-hpp
)
