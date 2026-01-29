# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# CMake script to generate GIR file for GstAnalyticsKeypoints metadata

find_program(G_IR_SCANNER g-ir-scanner)
find_program(G_IR_COMPILER g-ir-compiler)

if(NOT G_IR_SCANNER)
    message(WARNING "g-ir-scanner not found. GObject Introspection will not be available.")
    return()
endif()

# Set the namespace and version for your GIR file
set(GIR_NAMESPACE "DLStreamerMeta")
set(GIR_VERSION "1.0")
set(GIR_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${GIR_NAMESPACE}-${GIR_VERSION}.gir")
set(TYPELIB_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${GIR_NAMESPACE}-${GIR_VERSION}.typelib")

# Get GStreamer package information
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-analytics-1.0)

# Resolve GIR search paths via pkg-config to find Gst-1.0.gir
pkg_get_variable(GSTREAMER_GIRDIR gstreamer-1.0 girdir)

# Source and header files
set(KEYPOINTS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/gstanalyticskeypointsmtd.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/gstanalytics_gir.h"
)

set(KEYPOINTS_HEADERS
    "${CMAKE_SOURCE_DIR}/include/dlstreamer/gst/metadata/gstanalyticskeypointsmtd.h"
)

# Generate the GIR file
add_custom_command(
    OUTPUT ${GIR_OUTPUT}
    COMMAND ${G_IR_SCANNER}
        --warn-all
        --warn-error
        --namespace=${GIR_NAMESPACE}
        --nsversion=${GIR_VERSION}
        --identifier-prefix=GstAnalytics
        --symbol-prefix=gst_analytics
        --add-include-path=${GSTREAMER_GIRDIR}
        --include=Gst-1.0
        --include=GstAnalytics-1.0
        --library=dlstreamer_gst_meta
        --cflags-begin
        -I${CMAKE_SOURCE_DIR}/include
        ${GSTREAMER_CFLAGS}
        --cflags-end
        --output=${GIR_OUTPUT}
        --pkg=gstreamer-1.0
        --pkg=gstreamer-analytics-1.0
        ${KEYPOINTS_HEADERS}
        ${KEYPOINTS_SOURCES}
    DEPENDS ${KEYPOINTS_SOURCES} ${KEYPOINTS_HEADERS} ${TARGET_NAME}
    COMMENT "Generating GIR file for DLStreamerMeta"
)

# Compile GIR to typelib
add_custom_command(
    OUTPUT ${TYPELIB_OUTPUT}
    COMMAND ${G_IR_COMPILER}
        --output=${TYPELIB_OUTPUT}
        --includedir=${GSTREAMER_GIRDIR}
        ${GIR_OUTPUT}
    DEPENDS ${GIR_OUTPUT}
    COMMENT "Compiling GIR to typelib"
)

add_custom_target(keypoints_introspection ALL
    DEPENDS ${GIR_OUTPUT} ${TYPELIB_OUTPUT}
)

# Install GIR and typelib files
install(FILES ${GIR_OUTPUT}
    DESTINATION ${CMAKE_BINARY_DIR}/${BIN_FOLDER}/${CMAKE_BUILD_TYPE}/share/gir-1.0
)
install(FILES ${TYPELIB_OUTPUT}
    DESTINATION ${CMAKE_BINARY_DIR}/${BIN_FOLDER}/${CMAKE_BUILD_TYPE}/lib/girepository-1.0
)
