# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "test_latency_tracer")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTCHECK gstreamer-check-1.0 REQUIRED)

add_executable(${TARGET_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/test_latency_tracer.c)

target_include_directories(${TARGET_NAME}
PRIVATE
  ${GSTCHECK_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
PRIVATE
  ${GSTCHECK_LIBRARIES}
)

# The test loads the tracer as a plugin, so make sure it is built first.
add_dependencies(${TARGET_NAME} latency_tracer)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
