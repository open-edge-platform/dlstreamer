# ==============================================================================
# Copyright (C) 2019-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "test_metaaggregate")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTCHECK gstreamer-check-1.0 REQUIRED)
pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTALLOCATORS gstreamer-allocators-1.0 REQUIRED)

file (GLOB MAIN_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

file (GLOB MAIN_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_executable(${TARGET_NAME} ${MAIN_SRC} ${MAIN_HEADERS})

target_include_directories(${TARGET_NAME}
PRIVATE
  ${GSTCHECK_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
PRIVATE
  ${GSTCHECK_LIBRARIES}
  ${GSTVIDEO_LIBRARIES}
  ${GSTALLOCATORS_LIBRARIES}
  pipeline_test_common
  test_utils
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
