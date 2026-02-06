# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

cmake_minimum_required(VERSION 3.20)

set (TARGET_NAME "draw_face_attributes")

find_package(OpenCV OPTIONAL_COMPONENTS core imgproc)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTANALYTICS gstreamer-analytics-1.0>=1.16 REQUIRED)
pkg_check_modules(GLIB2 glib-2.0 REQUIRED)

file (GLOB MAIN_SRC *.cpp)

file (GLOB MAIN_HEADERS *.h)

set(GSTREAMER_INCLUDE_DIR /opt/intel/dlstreamer/gstreamer/include/gstreamer-1.0)
set(OPENCV_INCLUDE_DIR /usr/include/opencv4)
set(DLSTREAMER_INCLUDE_DIRS /opt/intel/dlstreamer/include)
link_directories(${TARGET_NAME} /opt/intel/dlstreamer/lib
        /opt/intel/dlstreamer/gstreamer/lib
        /usr/lib/x86_64-linux-gnu
)

add_executable(${TARGET_NAME} ${MAIN_SRC} ${MAIN_HEADERS})

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wno-deprecated-enum-enum-conversion HAVE_DEPRECATED_ENUM_CONVERSION)

if (HAVE_DEPRECATED_ENUM_CONVERSION)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-deprecated-enum-enum-conversion)
endif (HAVE_DEPRECATED_ENUM_CONVERSION)

set_target_properties(${TARGET_NAME} PROPERTIES CMAKE_CXX_STANDARD 23)

# use pkg-config if sample builds as standalone. Otherwise vars DLSTREAMER_INCLUDE_DIRS/etc set by top level cmake

target_include_directories(${TARGET_NAME}
    PRIVATE
        ${DLSTREAMER_BASE_DIR}/include/dlstreamer/gst
        ${DLSTREAMER_BASE_DIR}/include/
        ${GSTREAMER_INCLUDE_DIR}
        ${OPENCV_INCLUDE_DIR}
)

target_include_directories(${TARGET_NAME}
PRIVATE
        ${GSTVIDEO_INCLUDE_DIRS}
        ${GSTREAMER_INCLUDE_DIRS}
        ${GLIB2_INCLUDE_DIRS}
        ${DLSTREAMER_INCLUDE_DIRS}
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
)

if(NOT WIN32)
target_link_libraries(${TARGET_NAME} PRIVATE    
        -lopencv_core
        -lopencv_imgproc
)
endif()
