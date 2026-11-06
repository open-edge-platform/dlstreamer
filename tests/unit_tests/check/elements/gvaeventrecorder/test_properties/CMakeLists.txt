# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "test_gvaeventrecorder_properties")

add_executable(${TARGET_NAME} gvaeventrecorder_properties_tests.cpp)
target_link_libraries(${TARGET_NAME} PRIVATE gvaeventrecorder gtest)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})