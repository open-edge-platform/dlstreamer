# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

cmake_minimum_required(VERSION 3.5)

project(gstradarprocess)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_BASE REQUIRED gstreamer-base-1.0)

add_library(gstradarprocess SHARED
    gstradarprocess.cpp
    gstradarprocessmeta.c
)

target_include_directories(gstradarprocess
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTREAMER_BASE_INCLUDE_DIRS}
        ${DLSTREAMER_BASE_DIR}/thirdparty/json/include
)

target_link_libraries(gstradarprocess
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTREAMER_BASE_LIBRARIES}
        json-hpp
)

install(TARGETS gstradarprocess
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/gstreamer-1.0
)
