# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "gstdlstreamer_elements")

include_directories(${DLSTREAMER_BASE_DIR}/src/cpu/_plugin/)

add_subdirectory(batch_create)
add_subdirectory(batch_split)
add_subdirectory(capsrelax)
add_subdirectory(gvadrop)
add_subdirectory(gvafpscounter)
add_subdirectory(meta_aggregate)
add_subdirectory(roi_split)
add_subdirectory(video_frames_buffer)
add_subdirectory(meta_smooth)
add_subdirectory(gvainference)

file(GLOB MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/batch_create/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/batch_split/*.cpp
    ${DLSTREAMER_BASE_DIR}/src/cpu/tensor_postproc/*.cpp
    ${DLSTREAMER_BASE_DIR}/src/cpu/tensor_postproc/yolo/*.cpp
)

file(GLOB MAIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(${TARGET_NAME} SHARED ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

target_link_libraries(${TARGET_NAME}
PRIVATE
    batch_create
    batch_split
    capsrelax
    gvadrop
    gvafpscounter
    meta_aggregate
    roi_split
    meta_smooth
    video_frames_buffer
    dlstreamer_gst
    gstvideoanalyticsmeta
    gvainference
)

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
