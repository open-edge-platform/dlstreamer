# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#
# CMakeLists.txt template for a standalone DL Streamer / OpenVINO C++ application.
#
# Usage:
#   1. Copy this file as `CMakeLists.txt` into your application directory.
#   2. Replace `{{TARGET_NAME}}` with the application name (no spaces).
#   3. Remove sections marked OPTIONAL if your app does not need them
#      (OpenCV, custom compile flags).
#   4. Place all `.cpp` and `.h` files in the same directory as this CMakeLists.txt
#      (they are picked up via GLOB).
#
# Build:
#   mkdir build && cd build
#   cmake ..
#   make -j$(nproc)
# ==============================================================================

cmake_minimum_required(VERSION 3.20)

set(TARGET_NAME "{{TARGET_NAME}}")
project(${TARGET_NAME} CXX)

# --- Required dependencies ----------------------------------------------------
find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTREAMER     gstreamer-1.0>=1.16           REQUIRED)
pkg_check_modules(GSTVIDEO      gstreamer-video-1.0>=1.16     REQUIRED)
pkg_check_modules(GSTANALYTICS  gstreamer-analytics-1.0>=1.16 REQUIRED)
pkg_check_modules(GLIB2         glib-2.0                      REQUIRED)

# --- OPTIONAL: OpenCV (remove this block if your app does not use OpenCV) -----
find_package(OpenCV OPTIONAL_COMPONENTS core imgproc)

# --- DL Streamer install layout (default Intel DL Streamer install paths) -----
set(DLSTREAMER_INSTALL_PREFIX /opt/intel/dlstreamer)
set(DLSTREAMER_INCLUDE_DIRS   ${DLSTREAMER_INSTALL_PREFIX}/include)
set(GSTREAMER_INCLUDE_DIR     ${DLSTREAMER_INSTALL_PREFIX}/gstreamer/include/gstreamer-1.0)

link_directories(
    ${DLSTREAMER_INSTALL_PREFIX}/lib
    ${DLSTREAMER_INSTALL_PREFIX}/Release/lib
    ${DLSTREAMER_INSTALL_PREFIX}/gstreamer/lib
    /usr/lib/x86_64-linux-gnu
)

# --- Sources ------------------------------------------------------------------
file(GLOB MAIN_SRC     *.cpp)
file(GLOB MAIN_HEADERS *.h)

add_executable(${TARGET_NAME} ${MAIN_SRC} ${MAIN_HEADERS})

set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 23)

# --- OPTIONAL: silence noisy GStreamer enum-conversion warnings ---------------
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-deprecated-enum-enum-conversion HAVE_DEPRECATED_ENUM_CONVERSION)
if(HAVE_DEPRECATED_ENUM_CONVERSION)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-deprecated-enum-enum-conversion)
endif()

# --- Include directories ------------------------------------------------------
target_include_directories(${TARGET_NAME}
    PRIVATE
        ${DLSTREAMER_INCLUDE_DIRS}
        ${DLSTREAMER_INCLUDE_DIRS}/dlstreamer/gst
        ${GSTREAMER_INCLUDE_DIR}
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTVIDEO_INCLUDE_DIRS}
        ${GSTANALYTICS_INCLUDE_DIRS}
        ${GLIB2_INCLUDE_DIRS}
        ${OpenCV_INCLUDE_DIRS}     # OPTIONAL — remove if not using OpenCV
)

# --- Link libraries -----------------------------------------------------------
target_link_libraries(${TARGET_NAME}
    PUBLIC
        dlstreamer_gst_meta
    PRIVATE
        ${GLIB2_LIBRARIES}
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
        ${GSTANALYTICS_LIBRARIES}
        ${OpenCV_LIBS}             # OPTIONAL — remove if not using OpenCV
)
