# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "common")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.16 REQUIRED)
if(WIN32)
    pkg_check_modules(GSTD3D11 gstreamer-d3d11-1.0>=1.16 REQUIRED)
endif()
pkg_check_modules(GSTALLOC gstreamer-allocators-1.0 REQUIRED)
pkg_check_modules(GLIB2 glib-2.0 REQUIRED)


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
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTVIDEO_INCLUDE_DIRS}
        ${GLIB2_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}
PRIVATE
        ${GSTALLOC_INCLUDE_DIRS}
)

if(WIN32)
    target_include_directories(${TARGET_NAME} PUBLIC ${GSTD3D11_INCLUDE_DIRS} ${GSTD3D12_INCLUDE_DIRS})
    target_compile_definitions(${TARGET_NAME} PUBLIC GST_USE_UNSTABLE_API)
endif()

target_link_libraries(${TARGET_NAME}
PUBLIC
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
        ${GLIB2_LIBRARIES}
        dlstreamer_api
        logger
        gstvideoanalyticsmeta
        json-hpp
        utils
PRIVATE
        ${GSTALLOC_LIBRARIES}
)

if(WIN32)
    target_link_libraries(${TARGET_NAME} PUBLIC ${GSTD3D11_LIBRARIES} ${GSTD3D12_LIBRARIES})
endif()

if(${ENABLE_VAAPI} AND NOT(WIN32))
    target_link_libraries(${TARGET_NAME} PRIVATE va_api_wrapper)
endif()
