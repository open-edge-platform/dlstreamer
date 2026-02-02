# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "elements")

find_package(OpenCV REQUIRED core imgproc calib3d)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTALLOC gstreamer-allocators-1.0 REQUIRED)
if(NOT MSVC)
  pkg_check_modules(GSTVA REQUIRED IMPORTED_TARGET gstreamer-va-1.0)
endif()
if(${ENABLE_AUDIO_INFERENCE_ELEMENTS})
        pkg_check_modules(GSTAUDIO gstreamer-audio-1.0>=1.16 REQUIRED)
endif()

file (GLOB MAIN_SRC
    gvametaconvert/*.cpp
    gvametaconvert/*.c
    gvametaaggregate/*.cpp
    gvametaaggregate/*.c
    gvawatermark/*.cpp
    gvawatermark/renderer/*.cpp
    gvawatermark/renderer/cpu/*.cpp
    gvawatermark/*.c
    gvawatermark3d/*.cpp
    gvawatermark3d/*.c
    gvadeskew/*.cpp
    gvadeskew/*.c
)

# gvamotiondetect platform-specific source
if(MSVC OR WIN32)
    list(APPEND MAIN_SRC gvamotiondetect/gvamotiondetect_win.cpp)
else()
    list(APPEND MAIN_SRC gvamotiondetect/gvamotiondetect.cpp)
endif()

file (GLOB MAIN_HEADERS
    gvametaconvert/*.h
    gvametaaggregate/*.h
    gvawatermark/*.h
    gvawatermark/renderer/*.h
    gvawatermark/renderer/cpu/*.h
    gvawatermark3d/*.h
    gvadeskew/*.h
    gvamotiondetect/*.h
)

if(${ENABLE_AUDIO_INFERENCE_ELEMENTS})
    file (GLOB AUDIO_SRC
        gvametaconvert/audioconverter/*.cpp
    )
    file (GLOB AUDIO_HEADERS
        gvametaconvert/audioconverter/*.h
    )
    list (APPEND MAIN_SRC ${AUDIO_SRC})
    list (APPEND MAIN_HEADERS ${AUDIO_HEADERS})
endif()

add_library(${TARGET_NAME} STATIC ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    gvametaconvert
    gvawatermark
    gvawatermark/renderer
    gvametaaggregate
    gvawatermark3d
    gvadeskew
    gvamotiondetect
PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTVIDEO_INCLUDE_DIRS}
    ${GSTALLOC_INCLUDE_DIRS}
)

if(${ENABLE_AUDIO_INFERENCE_ELEMENTS})
    target_include_directories(${TARGET_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/gvametaconvert/audioconverter
        ${GSTAUDIO_INCLUDE_DIRS}
    )
endif()
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
    dlstreamer_gst_meta
    gstvideoanalyticsmeta
)

if(NOT MSVC)
    target_link_libraries(${TARGET_NAME} PRIVATE
        ${GSTVA_LIBRARIES}
        dlstreamer_vaapi
    )
endif()

if(${ENABLE_AUDIO_INFERENCE_ELEMENTS})
        target_link_libraries(${TARGET_NAME} PRIVATE ${GSTAUDIO_LIBRARIES})
endif()

add_subdirectory(gvaattachroi)
add_subdirectory(gvametapublish)
add_subdirectory(gvapython)
add_subdirectory(gvatrack)
if(ENABLE_REALSENSE)
    add_subdirectory(gvarealsense)
endif()
if(ENABLE_GENAI)
    add_subdirectory(gvagenai)
endif()
