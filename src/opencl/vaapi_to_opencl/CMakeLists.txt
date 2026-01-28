# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME vaapi_to_opencl)

find_package(OpenCL REQUIRED)

file(GLOB MAIN_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB MAIN_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

add_library(${TARGET_NAME} OBJECT ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
PRIVATE
        ${OpenCL_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
PUBLIC
        dlstreamer_api
        ${OpenCL_LIBRARY}
)
