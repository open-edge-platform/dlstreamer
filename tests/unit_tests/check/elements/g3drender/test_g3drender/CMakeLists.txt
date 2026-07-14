# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "test_g3drender")

add_executable(${TARGET_NAME} test_g3drender.cpp)

pkg_check_modules(GSTCHECK      gstreamer-check-1.0      REQUIRED)
pkg_check_modules(GSTANALYTICS gstreamer-analytics-1.0  REQUIRED)
pkg_check_modules(GSTVIDEO     gstreamer-video-1.0      REQUIRED)

target_include_directories(${TARGET_NAME}
PRIVATE
    ${GSTCHECK_INCLUDE_DIRS}
    ${GSTANALYTICS_INCLUDE_DIRS}
    ${GSTVIDEO_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(${TARGET_NAME} PRIVATE
    TEST_FILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../test_files"
)

target_link_libraries(${TARGET_NAME}
PRIVATE
    gstvideoanalyticsmeta
    ${GSTCHECK_LIBRARIES}
    ${GSTANALYTICS_LIBRARIES}
    ${GSTVIDEO_LIBRARIES}
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME}
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
