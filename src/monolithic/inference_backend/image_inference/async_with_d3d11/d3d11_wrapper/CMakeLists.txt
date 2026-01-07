# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

if(WIN32)
        set (TARGET_NAME "d3d11_wrapper")

        find_package(PkgConfig REQUIRED)

        # Find GStreamer D3D11
        pkg_check_modules(GSTD3D11 gstreamer-d3d11-1.0>=1.16 REQUIRED)

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
                ${GSTD3D11_INCLUDE_DIRS}
        )

        target_link_libraries(${TARGET_NAME}
        PUBLIC
                logger
                utils
                d3d11
                dxgi
                ${GSTD3D11_LIBRARIES}
        )
endif()
