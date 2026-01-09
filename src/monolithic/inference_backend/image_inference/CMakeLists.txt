# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "image_inference")

add_subdirectory(openvino)

if(${ENABLE_VAAPI} AND NOT(WIN32))
        add_subdirectory(async_with_va_api)
endif()

if(WIN32)
        add_subdirectory(async_with_d3d11)
endif()

find_package(PkgConfig REQUIRED)
find_package(OpenVINO REQUIRED Runtime)

file (GLOB MAIN_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
        )

file (GLOB MAIN_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
        )

add_library(${TARGET_NAME} STATIC ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/openvino
        ${CMAKE_CURRENT_SOURCE_DIR}/async_with_va_api
)

if(WIN32)
        target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/async_with_d3d11)
endif()

target_link_libraries(${TARGET_NAME}
PUBLIC
        openvino::runtime
        logger
        utils
)
