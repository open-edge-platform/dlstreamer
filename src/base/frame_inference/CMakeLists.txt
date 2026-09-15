# ==============================================================================
# Copyright (C) 2022-2024 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME frame_inference)

add_library(${TARGET_NAME} STATIC frame_inference.cpp frame_inference.hpp)
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${TARGET_NAME}
PUBLIC
    dlstreamer_api
    dlstreamer_logger
    model_proc
PRIVATE
    openvino_inference_backend
)

