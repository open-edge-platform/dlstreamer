# ==============================================================================
# Copyright (C) 2022-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "openvino_inference_backend")

find_package(OpenVINO REQUIRED Runtime)
pkg_search_module(VA va libva REQUIRED)
if(WIN32)
    pkg_check_modules(GSTD3D11 gstreamer-d3d11-1.0>=1.16 REQUIRED)
endif()
find_package(OpenCL REQUIRED)

add_library(${TARGET_NAME} OBJECT openvino.cpp openvino.hpp)
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${VA_INCLUDE_DIRS} ${OpenCL_INCLUDE_DIRS})

if(WIN32)
    target_include_directories(${TARGET_NAME} PUBLIC ${GSTD3D11_INCLUDE_DIRS})
endif()

target_link_libraries(${TARGET_NAME}
PUBLIC
    dlstreamer_api
PRIVATE
    openvino::runtime
    dlstreamer_logger
    OpenCL::OpenCL
)

if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE ${GSTD3D11_LIBRARIES})
endif()
