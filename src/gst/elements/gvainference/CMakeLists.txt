# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

cmake_minimum_required(VERSION 3.20)

set(TARGET_NAME "gvainference")

add_library(${TARGET_NAME} STATIC gvainference.cpp gvainference.h)
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${TARGET_NAME}
PUBLIC
    dlstreamer_gst
PRIVATE
    frame_inference
    dlstreamer_logger
    utils
    model_proc
)

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
