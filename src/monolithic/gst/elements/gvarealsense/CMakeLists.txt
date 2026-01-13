# ==============================================================================
# Copyright (C) 2025-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "gvarealsense")

find_package(OpenCV REQUIRED core imgproc)
find_package(realsense2)

file(GLOB REALSENSE_SRC
    *.cpp
)

file(GLOB REALSENSE_HEADERS
    *.h
)

add_library(gvarealsense SHARED gvarealsense.cpp gvarealsense.h gvarealsense_utils.cpp gvarealsense_utils.h
            gvarealsense_pcd.h gvarealsense_pcd.cpp )
set_compile_flags(${TARGET_NAME})

if(UNIX)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=unused-variable -Wno-error=unused-parameter)
endif()

target_include_directories(${TARGET_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTVIDEO_INCLUDE_DIRS}
        ${GSTALLOC_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
        ${GSTALLOC_LIBRARIES}
        ${OpenCV_LIBS}
        dlstreamer_api
        common
        image_inference
        image_inference_openvino
        logger
        json-hpp
        json-schema-validator
        utils
        realsense2
)

# Optionally, add install rules if needed