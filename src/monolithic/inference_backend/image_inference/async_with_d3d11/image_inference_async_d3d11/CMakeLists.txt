# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

if(WIN32)
        set (TARGET_NAME "image_inference_async_d3d11")

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
        )

        target_link_libraries(${TARGET_NAME}
        PRIVATE
                logger
                d3d11_wrapper
        )

        if(${ENABLE_ITT})
                target_link_libraries(${TARGET_NAME} PRIVATE ittnotify)
        endif()
endif()
