# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "gvaattachroi")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.14 REQUIRED)
pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.14 REQUIRED)
pkg_check_modules(GSTALLOC gstreamer-allocators-1.0 REQUIRED)
pkg_check_modules(GSTANALYTICS gstreamer-analytics-1.0>=1.16 REQUIRED)
pkg_check_modules(GLIB2 glib-2.0 REQUIRED)

file (GLOB ATTACHROI_SRC
        *.cpp
        *.c
)

file (GLOB ATTACHROI_HEADERS
        *.h
)

add_library(${TARGET_NAME} SHARED ${ATTACHROI_SRC} ${ATTACHROI_HEADERS})
set_compile_flags(${TARGET_NAME})

if(UNIX)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=unused-variable -Wno-error=unused-parameter)
endif()

target_include_directories(${TARGET_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

target_link_libraries(${TARGET_NAME}
    PRIVATE
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
        ${GSTALLOC_LIBRARIES}
        ${GSTANALYTICS_LIBRARIES}
        common
        dlstreamer_gst_meta
        utils
    )

install(TARGETS ${TARGET_NAME} DESTINATION lib/gst-video-analytics)
