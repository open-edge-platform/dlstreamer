# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

cmake_minimum_required(VERSION 3.20)
project(draw_face_attributes)
set (TARGET_NAME "draw_face_attributes")

find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTANALYTICS gstreamer-analytics-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTALLOCATORS gstreamer-allocators-1.0 REQUIRED)
pkg_check_modules(GLIB2 glib-2.0 REQUIRED)

if(DLSTREAMER_BASE_DIR)
    # In-tree build: DLSTREAMER_BASE_DIR is set by top-level CMakeLists.txt.
    # GStreamer/OpenCV are in build/deps/{gstreamer,opencv}-bin/.
    # Top-level already adds global include_directories and link_directories.
    set(DLSTREAMER_INCLUDE_DIRS
        ${DLSTREAMER_BASE_DIR}/include/dlstreamer/gst
        ${DLSTREAMER_BASE_DIR}/include
    )
    find_package(OpenCV OPTIONAL_COMPONENTS core imgproc)
else()
    # Standalone build: DL Streamer is installed at /opt/intel/dlstreamer.
    # PKG_CONFIG_PATH must be set to include dlstreamer's pkgconfig dirs
    # (done by build_and_run.sh or setup_dls_env.sh).
    set(DLSTREAMER_INSTALL_DIR "/opt/intel/dlstreamer")
    set(DLSTREAMER_INCLUDE_DIRS ${DLSTREAMER_INSTALL_DIR}/include)
    find_package(OpenCV
        OPTIONAL_COMPONENTS core imgproc
        PATHS ${DLSTREAMER_INSTALL_DIR}/opencv
    )
endif()

file (GLOB MAIN_SRC *.cpp)
file (GLOB MAIN_HEADERS *.h)

add_executable(${TARGET_NAME} ${MAIN_SRC} ${MAIN_HEADERS})

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-deprecated-enum-enum-conversion HAVE_DEPRECATED_ENUM_CONVERSION)

if (HAVE_DEPRECATED_ENUM_CONVERSION)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-deprecated-enum-enum-conversion)
endif (HAVE_DEPRECATED_ENUM_CONVERSION)

set_target_properties(${TARGET_NAME} PROPERTIES CMAKE_CXX_STANDARD 23)

target_include_directories(${TARGET_NAME}
PRIVATE
        ${DLSTREAMER_INCLUDE_DIRS}
        ${GSTVIDEO_INCLUDE_DIRS}
        ${GSTREAMER_INCLUDE_DIRS}
        ${GLIB2_INCLUDE_DIRS}
)

target_link_directories(${TARGET_NAME}
PRIVATE
        ${GSTVIDEO_LIBRARY_DIRS}
        ${GSTREAMER_LIBRARY_DIRS}
        ${GSTANALYTICS_LIBRARY_DIRS}
        ${GSTALLOCATORS_LIBRARY_DIRS}
)

target_link_libraries(${TARGET_NAME}
PUBLIC
        dlstreamer_gst_meta
PRIVATE
        ${OpenCV_LIBS}
        ${GLIB2_LIBRARIES}
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
        ${GSTANALYTICS_LIBRARIES}
        ${GSTALLOCATORS_LIBRARIES}
)

if(NOT WIN32 AND NOT OpenCV_FOUND)
    target_link_libraries(${TARGET_NAME} PRIVATE opencv_core opencv_imgproc)
endif()