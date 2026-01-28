# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME opencv_warp_affine)

find_package(OpenCV REQUIRED)
find_package(OpenCL REQUIRED)

add_library(${TARGET_NAME} STATIC opencv_warp_affine.cpp)
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
PRIVATE
        ${OpenCL_INCLUDE_DIRS}
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
PUBLIC
        dlstreamer_api
        ${OpenCV_LIBS}
        ${OpenCL_LIBRARY}
)

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})

