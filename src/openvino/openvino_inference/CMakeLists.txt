# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "openvino_inference")

find_package(OpenVINO REQUIRED)
if(WIN32)
    pkg_check_modules(GSTD3D11 gstreamer-d3d11-1.0>=1.16 REQUIRED)
endif()
find_package(OpenCL REQUIRED)

add_library(${TARGET_NAME} OBJECT openvino_inference.cpp)
set_compile_flags(${TARGET_NAME})

target_link_libraries(${TARGET_NAME}
PRIVATE
    openvino::runtime
    dlstreamer_api
    dlstreamer_logger
    OpenCL::OpenCL
)

if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE ${GSTD3D11_LIBRARIES})
endif()

target_include_directories(${TARGET_NAME}
PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../monolithic/inference_backend/include/inference_backend/
)

if(WIN32)
    target_include_directories(${TARGET_NAME} PRIVATE ${GSTD3D11_INCLUDE_DIRS})
endif()

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
