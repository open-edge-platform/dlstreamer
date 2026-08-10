# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTALLOC gstreamer-allocators-1.0 REQUIRED)
pkg_check_modules(GLIB2 glib-2.0 REQUIRED)

add_subdirectory(gst_logger_sink)
add_subdirectory(metadata)
add_subdirectory(utils)
add_subdirectory(lib)
add_subdirectory(elements)
add_subdirectory(bins)
add_subdirectory(tracers/buffer_tracer)

if (${ENABLE_ITT} AND NOT (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64" OR ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm"))
    add_subdirectory(tracers/gvaitttracer)
endif()
