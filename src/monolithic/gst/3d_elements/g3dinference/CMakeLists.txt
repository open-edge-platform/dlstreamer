# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "g3dinference")

find_package(PkgConfig REQUIRED)
find_package(OpenVINO REQUIRED Runtime)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_BASE REQUIRED gstreamer-base-1.0)

add_library(${TARGET_NAME} STATIC
    g3dinference.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTREAMER_BASE_LIBRARIES}
        openvino::runtime
        json-hpp
        dlstreamer_gst_meta
)