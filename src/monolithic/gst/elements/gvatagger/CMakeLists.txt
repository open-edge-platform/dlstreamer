# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "gvatagger")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_BASE REQUIRED gstreamer-base-1.0)
pkg_check_modules(GSTCODECPARSERS REQUIRED gstreamer-codecparsers-1.0)

add_library(${TARGET_NAME} STATIC
    gvatagger.cpp
)
set_compile_flags(${TARGET_NAME})

# codecparsers headers #warning without it, which -Werror turns into a build failure.
target_compile_definitions(${TARGET_NAME} PUBLIC GST_USE_UNSTABLE_API)

target_include_directories(${TARGET_NAME} PUBLIC
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_BASE_INCLUDE_DIRS}
    ${GSTCODECPARSERS_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${TARGET_NAME}
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_BASE_LIBRARIES}
    ${GSTCODECPARSERS_LIBRARIES}
)
