# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

cmake_minimum_required(VERSION 3.5)

project(gstradarprocessor)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_BASE REQUIRED gstreamer-base-1.0)

add_library(gstradarprocessor SHARED
    gstradarprocessor.cpp
    gstradarprocessormeta.c
)

target_include_directories(gstradarprocessor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTREAMER_BASE_INCLUDE_DIRS}
        ${DLSTREAMER_BASE_DIR}/thirdparty/json/include
)

target_link_libraries(gstradarprocessor
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTREAMER_BASE_LIBRARIES}
        json-hpp
)

install(TARGETS gstradarprocessor
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/gstreamer-1.0
)
