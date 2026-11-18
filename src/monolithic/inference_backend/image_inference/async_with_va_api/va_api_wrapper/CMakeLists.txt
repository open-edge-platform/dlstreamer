# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "va_api_wrapper")

find_package(PkgConfig)
find_package(OpenCL REQUIRED)
pkg_search_module(VA va libva REQUIRED)
if(UNIX)
    pkg_search_module(VADRM va-drm libva-drm REQUIRED)
endif()

file (GLOB MAIN_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file (GLOB MAIN_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(${TARGET_NAME} STATIC ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${VA_INCLUDE_DIRS}
PRIVATE
        ${VADRM_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
PUBLIC
        logger
        pre_proc
        opencv_pre_proc
        utils
        ${VA_LIBRARIES}
        OpenCL::OpenCL
PRIVATE
        ${VADRM_LIBRARIES}
        utils
)
