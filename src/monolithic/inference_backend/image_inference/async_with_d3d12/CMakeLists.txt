# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

if(WIN32)
    set (TARGET_NAME "image_inference_async_d3d12")

    find_package(PkgConfig REQUIRED)
    find_package(OpenVINO REQUIRED Runtime)
    find_package(OpenCL REQUIRED)

    pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.16 REQUIRED)
    # GSTD3D12_INCLUDE_DIRS / GSTD3D12_LIBRARIES come from the top-level CMakeLists.txt.

    set(MAIN_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/d3d12_converter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/d3d12_surface_sharing.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/image_inference_async_d3d12.cpp
    )

    add_library(${TARGET_NAME} STATIC ${MAIN_SRC})
    set_compile_flags(${TARGET_NAME})

    target_compile_definitions(${TARGET_NAME} PRIVATE GST_USE_UNSTABLE_API)

    target_include_directories(${TARGET_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GSTD3D12_INCLUDE_DIRS}
        ${GSTVIDEO_INCLUDE_DIRS}
    )

    target_link_libraries(${TARGET_NAME}
    PUBLIC
        logger
        utils
        d3d12
        dxgi
        openvino::runtime
        OpenCL::OpenCL
        ${GSTD3D12_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
    )

    if(${ENABLE_ITT})
        target_link_libraries(${TARGET_NAME} PRIVATE ittnotify)
    endif()
endif()
