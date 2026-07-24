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
find_package(CURL)

# Add executable
set(GVAGENAI_SOURCES
    gstgvagenai.cpp
    configs.cpp
    backends/genai_backend.cpp
    backends/frame_utils.cpp
    backends/openvino/genai.cpp
    backends/openvino/openvino_backend.cpp
)

if(CURL_FOUND)
    list(APPEND GVAGENAI_SOURCES backends/openai-http/openai_http_backend.cpp)
else()
    message(WARNING "libcurl not found - gvagenai's 'openai-http' backend will not be available")
endif()

add_library(${TARGET_NAME} STATIC ${GVAGENAI_SOURCES})

# Include directories
target_include_directories(${TARGET_NAME} PUBLIC
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_BASE_INCLUDE_DIRS}
    ${GSTREAMER_VIDEO_INCLUDE_DIRS}
    ${OpenVINO_INCLUDE_DIRS}
    ${DLSTREAMER_BASE_DIR}/include/dlstreamer/gst/metadata
    ${CMAKE_CURRENT_SOURCE_DIR}
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

if(CURL_FOUND)
    target_link_libraries(${TARGET_NAME} CURL::libcurl)
    target_compile_definitions(${TARGET_NAME} PRIVATE GVAGENAI_HAVE_HTTP_BACKEND)
endif()
