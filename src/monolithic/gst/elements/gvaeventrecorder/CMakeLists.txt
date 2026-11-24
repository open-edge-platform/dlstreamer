# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "gvaeventrecorder")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_BASE REQUIRED gstreamer-base-1.0)
pkg_check_modules(GSTREAMER_VIDEO REQUIRED gstreamer-video-1.0)
pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GSTANALYTICS REQUIRED gstreamer-analytics-1.0>=1.16)

set(GVAEVENTRECORDER_SOURCES
    gstgvaeventrecorder.cpp
    event_session_controller/event_session_controller.cpp
    video_clip_writer/video_clip_writer.cpp
)

add_library(${TARGET_NAME} STATIC ${GVAEVENTRECORDER_SOURCES})

target_include_directories(${TARGET_NAME} PUBLIC
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_BASE_INCLUDE_DIRS}
    ${GSTREAMER_VIDEO_INCLUDE_DIRS}
    ${GSTREAMER_APP_INCLUDE_DIRS}
    ${DLSTREAMER_BASE_DIR}/include/dlstreamer/gst/metadata
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${TARGET_NAME}
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_BASE_LIBRARIES}
    ${GSTREAMER_VIDEO_LIBRARIES}
    ${GSTREAMER_APP_LIBRARIES}
    ${GSTANALYTICS_LIBRARIES}
    common
    dlstreamer_gst_meta
)
