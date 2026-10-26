# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(COMMON_LIBS test_common test_utils)

# --- Display config parsing tests ---
set(TARGET_CONFIG "test_gvawatermark_config")
add_executable(${TARGET_CONFIG} ${CMAKE_CURRENT_SOURCE_DIR}/test_watermark_config.cpp)
target_link_libraries(${TARGET_CONFIG} PRIVATE ${COMMON_LIBS})
add_test(NAME ${TARGET_CONFIG} COMMAND ${TARGET_CONFIG} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# --- Caps negotiation tests ---
set(TARGET_CAPS "test_gvawatermark_caps")
add_executable(${TARGET_CAPS} ${CMAKE_CURRENT_SOURCE_DIR}/test_watermark_caps.cpp)
target_link_libraries(${TARGET_CAPS} PRIVATE ${COMMON_LIBS})
add_test(NAME ${TARGET_CAPS} COMMAND ${TARGET_CAPS} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# --- Producer/consumer segmentation mask dims contract test (Layer 3) ---
set(TARGET_CONTRACT "test_gvawatermark_segmentation_contract")
add_executable(${TARGET_CONTRACT} ${CMAKE_CURRENT_SOURCE_DIR}/test_watermark_segmentation_contract.cpp)
target_link_libraries(${TARGET_CONTRACT} PRIVATE ${COMMON_LIBS} inference_elements)
add_test(NAME ${TARGET_CONTRACT} COMMAND ${TARGET_CONTRACT} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# --- RGB to YUV color converter tests (no GStreamer pipeline) ---
find_package(OpenCV REQUIRED core)
set(WATERMARK_RENDERER_DIR ${DLSTREAMER_BASE_DIR}/src/monolithic/gst/elements/gvawatermark/renderer)
set(TARGET_COLOR_CONVERTER "test_gvawatermark_color_converter")
add_executable(${TARGET_COLOR_CONVERTER}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_watermark_color_converter.cpp
    ${WATERMARK_RENDERER_DIR}/color_converter.cpp)
target_include_directories(${TARGET_COLOR_CONVERTER} PRIVATE ${WATERMARK_RENDERER_DIR})
target_link_libraries(${TARGET_COLOR_CONVERTER} PRIVATE gtest dlstreamer_api ${OpenCV_LIBS})
add_test(NAME ${TARGET_COLOR_CONVERTER} COMMAND ${TARGET_COLOR_CONVERTER}
         WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
