# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

if(WIN32)
    set (TARGET_NAME "image_inference_async_d3d11")

    find_package(PkgConfig REQUIRED)

    pkg_check_modules(GSTD3D11 gstreamer-d3d11-1.0>=1.16 REQUIRED)
    pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.16 REQUIRED)

    set(MAIN_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/d3d11_converter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/image_inference_async_d3d11.cpp
    )

    add_library(${TARGET_NAME} STATIC ${MAIN_SRC})
    set_compile_flags(${TARGET_NAME})

    target_compile_definitions(${TARGET_NAME} PRIVATE GST_USE_UNSTABLE_API)

    target_include_directories(${TARGET_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${GSTD3D11_INCLUDE_DIRS}
        ${GSTVIDEO_INCLUDE_DIRS}
    )

    target_link_libraries(${TARGET_NAME}
    PUBLIC
        logger
        utils
        d3d11
        dxgi
        ${GSTD3D11_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
    )

    if(${ENABLE_ITT})
        target_link_libraries(${TARGET_NAME} PRIVATE ittnotify)
    endif()
endif()
