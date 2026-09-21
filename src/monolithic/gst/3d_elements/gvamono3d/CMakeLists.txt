# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "gvamono3d")

find_package(PkgConfig REQUIRED)
find_package(OpenVINO REQUIRED Runtime)
find_package(OpenCL REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_BASE REQUIRED gstreamer-base-1.0)
pkg_check_modules(GSTVIDEO REQUIRED gstreamer-video-1.0)
pkg_check_modules(GSTANALYTICS gstreamer-analytics-1.0>=1.28 REQUIRED)
pkg_check_modules(GSTVA REQUIRED IMPORTED_TARGET gstreamer-va-1.0)
pkg_search_module(VA va libva REQUIRED)

add_library(${TARGET_NAME} STATIC
    gvamono3d.cpp
)
set_compile_flags(${TARGET_NAME})

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
        ${GSTVIDEO_INCLUDE_DIRS}
        ${GSTANALYTICS_INCLUDE_DIRS}
        ${GSTVA_INCLUDE_DIRS}
        ${VA_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${DLSTREAMER_BASE_DIR}/src/utils
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTREAMER_BASE_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
        ${GSTANALYTICS_LIBRARIES}
        ${GSTVA_LIBRARIES}
        ${VA_LIBRARIES}
        OpenCL::OpenCL
        openvino::runtime
        json-hpp
        dlstreamer_gst_meta
)
