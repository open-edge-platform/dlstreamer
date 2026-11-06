# ============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ============================================================================

set(TARGET_NAME "test_gvaeventrecorder")

add_executable(${TARGET_NAME}
	event_recorder_logic_tests.cpp
)
target_link_libraries(${TARGET_NAME} PRIVATE gvaeventrecorder gtest)

pkg_check_modules(GSTCHECK gstreamer-check-1.0 REQUIRED)
target_include_directories(${TARGET_NAME} PRIVATE ${GSTCHECK_INCLUDE_DIRS})
target_link_libraries(${TARGET_NAME} PRIVATE ${GSTCHECK_LIBRARIES})

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})