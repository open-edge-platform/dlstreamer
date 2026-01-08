# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "gstvideoanalytics")

find_package(PkgConfig REQUIRED)
pkg_search_module(VA va libva REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)

add_library(${TARGET_NAME} SHARED register_elements.cpp)
set_compile_flags(${TARGET_NAME})

if(ENABLE_GENAI)
    target_compile_definitions(${TARGET_NAME} PRIVATE ENABLE_GENAI)
endif()

if(UNIX)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=unused-variable -Wno-error=unused-parameter)
endif()

target_include_directories(${TARGET_NAME}
PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../elements/gvagenai/
    ${CMAKE_CURRENT_SOURCE_DIR}/../elements/gvametaaggregate/
    ${CMAKE_CURRENT_SOURCE_DIR}/../elements/gvametaconvert/
    ${CMAKE_CURRENT_SOURCE_DIR}/../elements/gvametapublish/
    ${CMAKE_CURRENT_SOURCE_DIR}/../elements/gvametapublish/base/
    ${CMAKE_CURRENT_SOURCE_DIR}/../elements/gvametapublish/file/
    ${CMAKE_CURRENT_SOURCE_DIR}/../elements/gvatrack/
    ${CMAKE_CURRENT_SOURCE_DIR}/../elements/gvawatermark/
    ${CMAKE_CURRENT_SOURCE_DIR}/../elements/gvawatermark3d/
    ${CMAKE_CURRENT_SOURCE_DIR}/../elements/gvamotiondetect/
    ${CMAKE_CURRENT_SOURCE_DIR}/../elements/gvadeskew/
)

target_link_directories(${TARGET_NAME} PUBLIC ${VA_LIBRARY_DIRS})

target_link_libraries(${TARGET_NAME}
PRIVATE
    ${GSTREAMER_LIBRARIES}
    common
    elements
    inference_elements
    audio_inference_elements
    gvatrack
    gstdlstreamer_bins
    gvametapublish
)

if(ENABLE_GENAI)
    target_link_libraries(${TARGET_NAME} PRIVATE gvagenai)
endif()

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
