# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTCHECK gstreamer-check-1.0 REQUIRED)

# Tracer parameters are read once per process from GST_TRACERS, so each configuration is its own binary:
# test_latency_tracer runs without post-metrics, test_latency_tracer_metrics with post-metrics=true.
foreach(TARGET_NAME test_latency_tracer test_latency_tracer_metrics)
    add_executable(${TARGET_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/${TARGET_NAME}.c)

    target_include_directories(${TARGET_NAME}
    PRIVATE
      ${GSTCHECK_INCLUDE_DIRS}
    )

    target_link_libraries(${TARGET_NAME}
    PRIVATE
      ${GSTCHECK_LIBRARIES}
    )
    if(UNIX)
        target_link_libraries(${TARGET_NAME} PRIVATE m)
    endif()

    # The test loads the tracer as a plugin, so make sure it is built first.
    add_dependencies(${TARGET_NAME} latency_tracer)

    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endforeach()
