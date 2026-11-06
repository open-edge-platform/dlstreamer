# ============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ============================================================================

set(TARGET_NAME "test_gvaroiscale_properties")

add_executable(${TARGET_NAME} gvaroiscale_properties_tests.cpp)
target_link_libraries(${TARGET_NAME} PRIVATE gvaroiscale gtest)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})