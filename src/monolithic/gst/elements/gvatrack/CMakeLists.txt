# ==============================================================================
# Copyright (C) 2020-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "gvatrack")

find_package(OpenCV REQUIRED core imgproc)
find_package(OpenVINO REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)

file (GLOB_RECURSE MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR} *.c *.cpp
)
file (GLOB MAIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(${TARGET_NAME} STATIC ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

if(UNIX)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=unused-variable -Wno-error=unused-parameter -Wno-error=reorder)
endif()

target_include_directories(${TARGET_NAME}
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
PRIVATE
    ${GLIB2_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/vas/modules/api/ot/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/vas/modules/common/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/vas/modules/platform/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/vas/modules/kernels/kalman_filter/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/vas/modules/components/ot/inc
)

target_link_libraries(${TARGET_NAME}
PRIVATE
    ${GSTREAMER_LIBRARIES}
    ${GLIB2_LIBRARIES}
    common
    utils
    openvino::runtime
    ${ADDITIONAL_LINKS}
)
