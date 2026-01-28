# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "gstvideoanalyticsmeta")

find_package(OpenCV REQUIRED core imgproc)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTANALYTICS gstreamer-analytics-1.0>=1.16 REQUIRED)
pkg_check_modules(GLIB2 glib-2.0 REQUIRED)

if(${ENABLE_AUDIO_INFERENCE_ELEMENTS})
        pkg_check_modules(GSTAUDIO gstreamer-audio-1.0>=1.16 REQUIRED)
endif()

add_library(${TARGET_NAME} INTERFACE)

target_include_directories(${TARGET_NAME}
INTERFACE
        ${GSTREAMER_INCLUDE_DIRS}
        ${GLIB2_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/metadata
)

if(${ENABLE_AUDIO_INFERENCE_ELEMENTS})
    target_include_directories(${TARGET_NAME}
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/audio
        ${DLSTREAMER_BASE_DIR}/include/dlstreamer/gst
        ${DLSTREAMER_BASE_DIR}/include/dlstreamer/gst/metadata
        ${GSTAUDIO_INCLUDE_DIRS}
    )
endif()

target_link_libraries(${TARGET_NAME}
INTERFACE
        dlstreamer_gst_meta
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
        ${GSTANALYTICS_LIBRARIES}
        ${OpenCV_LIBS}
        ${GLIB2_LIBRARIES}
)

if(${ENABLE_AUDIO_INFERENCE_ELEMENTS})
        target_link_libraries(${TARGET_NAME} INTERFACE ${GSTAUDIO_LIBRARIES})
endif()

configure_file(${CMAKE_SOURCE_DIR}/cmake/dl-streamer.pc.in ${CMAKE_BINARY_DIR}/${BIN_FOLDER}/${CMAKE_BUILD_TYPE}/lib/pkgconfig/dl-streamer.pc @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/${BIN_FOLDER}/${CMAKE_BUILD_TYPE}/lib/pkgconfig/dl-streamer.pc DESTINATION ${DLSTREAMER_LIBRARIES_INSTALL_PATH}/pkgconfig/)
