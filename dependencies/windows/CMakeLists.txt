# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
#
# Standalone build for gstanalytics-1.0-0.dll from a patched gst-plugins-bad
# source tree. Driven by build_gstanalytics_zip.ps1; not added to the main
# DL Streamer build.
#
# Required cache variables:
#   GST_ANALYTICS_SRC_DIR   - subprojects/gst-plugins-bad/gst-libs/gst/analytics
#                             inside the patched gstreamer checkout
#   GSTREAMER_PREFIX        - existing GStreamer install 

cmake_minimum_required(VERSION 3.20)
project(gstanalytics LANGUAGES C)

if(NOT WIN32)
    message(FATAL_ERROR "This CMake project is Windows-only.")
endif()

if(NOT GST_ANALYTICS_SRC_DIR OR NOT EXISTS "${GST_ANALYTICS_SRC_DIR}")
    message(FATAL_ERROR "GST_ANALYTICS_SRC_DIR must point to a patched gst-plugins-bad analytics source dir")
endif()

if(NOT GSTREAMER_PREFIX OR NOT EXISTS "${GSTREAMER_PREFIX}")
    message(FATAL_ERROR "GSTREAMER_PREFIX must point to an existing GStreamer install")
endif()

# Order kept deterministic so the resulting .lib has stable contents.
set(GSTANALYTICS_SOURCES
    gstanalyticsmeta.c
    gstanalyticsclassificationmtd.c
    gstanalyticsobjectdetectionmtd.c
    gstanalyticsobjecttrackingmtd.c
    gstanalyticssegmentationmtd.c
    gstanalyticsgroupmtd.c
    gstanalyticskeypointmtd.c
    gstanalyticsbatchmeta.c
    gstanalyticstensormtd.c
    gsttensormeta.c
    gsttensor.c
    gstanalytics_image_util.c
    modelinfo.c
)

set(_full_sources "")
foreach(_s ${GSTANALYTICS_SOURCES})
    set(_p "${GST_ANALYTICS_SRC_DIR}/${_s}")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "Missing source after applying patch: ${_p}")
    endif()
    list(APPEND _full_sources "${_p}")
endforeach()

# Stub config.h — an empty header is sufficient for the analytics sources.
file(WRITE "${CMAKE_BINARY_DIR}/config.h" "/* Auto-generated stub */\n")

add_library(gstanalytics-1.0-0 SHARED ${_full_sources})

set_target_properties(gstanalytics-1.0-0 PROPERTIES
    OUTPUT_NAME              "gstanalytics-1.0-0"
    PREFIX                   ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/bin"
)

# The sources include their own headers as <gst/analytics/...>, so
# the gst-libs root must be on the include path. GST_ANALYTICS_SRC_DIR points
# to .../gst-libs/gst/analytics — its grandparent is gst-libs.
get_filename_component(_gst_libs_dir "${GST_ANALYTICS_SRC_DIR}/../.." ABSOLUTE)

target_include_directories(gstanalytics-1.0-0 PRIVATE
    "${CMAKE_BINARY_DIR}"
    "${GST_ANALYTICS_SRC_DIR}"
    "${_gst_libs_dir}"
    "${GSTREAMER_PREFIX}/include/gstreamer-1.0"
    "${GSTREAMER_PREFIX}/include/glib-2.0"
    "${GSTREAMER_PREFIX}/lib/glib-2.0/include"
    "${GSTREAMER_PREFIX}/include/orc-0.4"
)

# BUILDING_GST_ANALYTICS controls dllexport via analytics-meta-prelude.h.
target_compile_definitions(gstanalytics-1.0-0 PRIVATE
    HAVE_CONFIG_H
    GST_USE_UNSTABLE_API
    BUILDING_GST_ANALYTICS
    G_LOG_DOMAIN="GStreamer-Analytics"
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    G_DISABLE_CAST_CHECKS
)

if(MSVC)
    target_compile_options(gstanalytics-1.0-0 PRIVATE
        /utf-8 /W2 /wd4018 /wd4146 /wd4244 /wd4305 /wd5051
        /Zi  # emit PDB
    )
    target_link_options(gstanalytics-1.0-0 PRIVATE /DEBUG)
endif()

set(_gst_lib_dir "${GSTREAMER_PREFIX}/lib")
target_link_directories(gstanalytics-1.0-0 PRIVATE "${_gst_lib_dir}")
target_link_libraries(gstanalytics-1.0-0 PRIVATE
    gstbase-1.0
    gstreamer-1.0
    gstvideo-1.0
    gobject-2.0
    glib-2.0
    intl
)

# Collect outputs into staging dir.
set(STAGE_DIR "${CMAKE_BINARY_DIR}/stage" CACHE PATH "Zip staging directory")
file(MAKE_DIRECTORY "${STAGE_DIR}/bin")
file(MAKE_DIRECTORY "${STAGE_DIR}/lib")
file(MAKE_DIRECTORY "${STAGE_DIR}/lib/girepository-1.0")
file(MAKE_DIRECTORY "${STAGE_DIR}/include/gstreamer-1.0/gst/analytics")
file(MAKE_DIRECTORY "${STAGE_DIR}/share/gir-1.0")

# Public headers shipped in the patched GStreamer install.
set(GSTANALYTICS_PUBLIC_HEADERS
    analytics.h
    gstanalyticsmeta.h
    gstanalyticsgroupmtd.h
    gstanalyticskeypointmtd.h
)

add_custom_command(TARGET gstanalytics-1.0-0 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:gstanalytics-1.0-0>"
        "${STAGE_DIR}/bin/gstanalytics-1.0-0.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_LINKER_FILE:gstanalytics-1.0-0>"
        "${STAGE_DIR}/lib/gstanalytics-1.0.lib"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_PDB_FILE:gstanalytics-1.0-0>"
        "${STAGE_DIR}/bin/gstanalytics-1.0-0.pdb"
    VERBATIM
)

foreach(_h ${GSTANALYTICS_PUBLIC_HEADERS})
    add_custom_command(TARGET gstanalytics-1.0-0 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GST_ANALYTICS_SRC_DIR}/${_h}"
            "${STAGE_DIR}/include/gstreamer-1.0/gst/analytics/${_h}"
        VERBATIM
    )
endforeach()
