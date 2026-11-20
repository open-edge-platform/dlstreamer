# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# OpenCL pre-processing backend (pre-process-backend=opencl), used by the OpenVINO image inference.

set (TARGET_NAME "image_inference_opencl")

find_package(OpenCL REQUIRED)

set(MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/opencl_context.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/opencl_frame_source_host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/opencl_preproc_geometry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/opencl_preprocessor.cpp
)

if(WIN32)
    list(APPEND MAIN_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/opencl_frame_source_d3d11.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/image_inference_async_opencl.cpp
    )
elseif(${ENABLE_VAAPI})
    list(APPEND MAIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/opencl_frame_source_va.cpp)
endif()

file (GLOB MAIN_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_library(${TARGET_NAME} STATIC ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${TARGET_NAME}
PUBLIC
    logger
    utils
    OpenCL::OpenCL
)

if(WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTD3D11 gstreamer-d3d11-1.0>=1.16 REQUIRED)
    target_compile_definitions(${TARGET_NAME} PRIVATE GST_USE_UNSTABLE_API)
    target_include_directories(${TARGET_NAME} PRIVATE ${GSTD3D11_INCLUDE_DIRS})
    target_link_libraries(${TARGET_NAME} PUBLIC d3d11 ${GSTD3D11_LIBRARIES})
elseif(${ENABLE_VAAPI})
    target_link_libraries(${TARGET_NAME} PUBLIC va_api_wrapper)
endif()

if(${ENABLE_ITT})
    target_link_libraries(${TARGET_NAME} PRIVATE ittnotify)
endif()
