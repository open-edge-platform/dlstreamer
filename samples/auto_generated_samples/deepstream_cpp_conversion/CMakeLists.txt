# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

cmake_minimum_required(VERSION 3.20)
project(deepstream_lpr_app)
set(TARGET_NAME "deepstream-lpr-app")

find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTANALYTICS gstreamer-analytics-1.0>=1.16 REQUIRED)
pkg_check_modules(GLIB2 glib-2.0 REQUIRED)

add_executable(${TARGET_NAME} deepstream_lpr_app.cpp)

target_include_directories(${TARGET_NAME} PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTANALYTICS_INCLUDE_DIRS}
    ${GLIB2_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME} PRIVATE
    ${GSTREAMER_LIBRARIES}
    ${GSTANALYTICS_LIBRARIES}
    ${GLIB2_LIBRARIES}
)

target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)
