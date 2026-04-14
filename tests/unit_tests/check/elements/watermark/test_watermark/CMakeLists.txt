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
