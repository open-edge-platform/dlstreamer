# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "image_inference_openvino")

find_package(PkgConfig REQUIRED)
find_package(OpenVINO REQUIRED Runtime)
find_package(OpenCL REQUIRED)

if(ENABLE_SYSTEM_FMT)
    find_package(fmt CONFIG REQUIRED)
    set(FMT_TARGET fmt::fmt)
else()
    set(FMT_TARGET spdlog::spdlog)
endif()

file (GLOB MAIN_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
        )

file (GLOB MAIN_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
        )
add_library(${TARGET_NAME} STATIC ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

# FIXME: there are some debug information that are removed for release build type
# FIXME: hence it marked as error
if(UNIX)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=unused-parameter)
endif()

target_include_directories(${TARGET_NAME}
PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${TARGET_NAME}
PUBLIC
        openvino::runtime
        utils
        logger
        ${FMT_TARGET}
        OpenCL::OpenCL
)

if(ENABLE_SYSTEM_FMT)
    target_compile_definitions(${TARGET_NAME} PRIVATE DLS_USE_SYSTEM_FMT)
endif()

if(WIN32)
    target_link_libraries(${TARGET_NAME} PUBLIC d3d11)
endif()

if (${ENABLE_VAAPI} AND NOT(WIN32))
        add_definitions(-DENABLE_GPU_TILE_AFFINITY)
        target_link_libraries(${TARGET_NAME} PUBLIC va_api_wrapper)
endif()
